// In-process Cling engine — code moved verbatim from src/main.cpp as step 1
// of docs/plans/client-server-refactor.md. The interpreter setup, syntax
// validation, compile blocks, and the AST introspection that recovers the
// slide's returned lambda all live here now; main.cpp only calls
// compileSetup()/compileSlide() at the same points in the frame loop.
#include "ClingEngine.h"

#include "MarkerExtract.h"
#include "Presentation.h"
#include "system/sys_util.h"
#include "system/stdcapture.h"

#include "clang/AST/Mangle.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Interpreter/IncrementalCUDADeviceCompiler.h"
#include "cling/Utils/Output.h"
#include "cling/MetaProcessor/InputValidator.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

#include <fmt/format.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

// True when the extracted result expression is just a (possibly
// address-taken) identifier — e.g. "update" or "&update" — as opposed to a
// self-contained expression like a lambda literal.
bool isPlainName(const std::string &expr) {
    std::string s = expr;
    if (!s.empty() && s[0] == '&')
        s = s.substr(1);
    if (s.empty() || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_')
            return false;
    return true;
}

std::string exprToString(clang::Expr* expr, const clang::ASTContext& context) {
    clang::LangOptions langOpts;
    langOpts.CPlusPlus = true;
    clang::PrintingPolicy policy(langOpts);
    policy.AnonymousTagLocations = false;
    policy.SuppressUnwrittenScope = true;

    std::string str;
    llvm::raw_string_ostream os(str);
    expr->printPretty(os, nullptr, policy);
    return os.str();
}

std::string findResultExprFromExtractionFunction(cling::Transaction* tx) {
    for (auto it = tx->rdecls_begin(); it != tx->rdecls_end(); ++it) {
        for (clang::DeclGroupRef::const_iterator I = it->m_DGR.begin(), E = it->m_DGR.end(); I != E; ++I) {

            auto* func = llvm::dyn_cast<clang::FunctionDecl>(*I);
            if (!func) continue;
            // Apparently not all functions have a name, but we can always getNameAsString().
            //if (!func->getName().starts_with("__cling_")) continue;
            if (!func->getNameAsString().starts_with("__cling_")) continue;

            // From ValueExtractionSynthesizer.cpp:
            // We need to synthesize later:
            // Wrapper has signature: void w(cling::Value SVR)
            // case 1):
            //   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
            // case 2):
            //   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)
            // case 2.1):
            //   copyArray(src, placement, size)
            if (auto* body = llvm::dyn_cast<clang::CompoundStmt>(func->getBody())) {
                for (auto* stmt : body->body()) {
                    clang::CallExpr* call = nullptr;
                    clang::CXXNewExpr* cxxNew = nullptr;
                    // There are two cases: explicit return statement or implicit return of the last expression.
                    if (auto* ret = llvm::dyn_cast<clang::ReturnStmt>(stmt)) {
                        if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(ret->getRetValue()->IgnoreImpCasts())) {
                            call = maybeCall;
                        }
                        if (auto * maybeCxxNew = llvm::dyn_cast<clang::CXXNewExpr>(ret->getRetValue()->IgnoreImpCasts())) {
                            cxxNew = maybeCxxNew;
                        }
                    } else if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(stmt)) {
                        call = maybeCall;
                    } else if (auto* maybeCxxNew = llvm::dyn_cast<clang::CXXNewExpr>(stmt)) {
                        cxxNew = maybeCxxNew;
                    }
                    if (call) {
                        if (auto* callee = call->getDirectCallee()) {
                            // case 1): lastExpr is the fourth argument of setValueNoAlloc
                            if (callee->getName() == "setValueNoAlloc") {
                                if (call->getNumArgs() >= 5) {
                                    clang::Expr* last_arg = call->getArg(4)->IgnoreImpCasts();
                                    // if (auto* decl_ref = llvm::dyn_cast<clang::DeclRefExpr>(last_arg)) {
                                    //     return decl_ref->getDecl()->getNameAsString();
                                    // }
                                    std::string expr = exprToString(last_arg, func->getASTContext());
                                    if (expr.starts_with("(void *)")) {
                                        expr = expr.substr(8); // Remove "(void *)"
                                    }
                                    return expr;
                                }
                            }
                        }
                    }
                    if (cxxNew) {
                        // case 2): lastExpr is the argument of placement new
                        //          but we need to check if the destination address is a call to setValueWithAlloc
                        bool is_placement_new = cxxNew->getNumPlacementArgs() > 0;
                        if (is_placement_new) {
                            if (auto* maybeCall = llvm::dyn_cast<clang::CallExpr>(cxxNew->getPlacementArg(0)->IgnoreImpCasts())) {
                                call = maybeCall;
                            }
                            if (call) {
                                if (auto* callee = call->getDirectCallee()) {
                                    if (callee->getName() == "setValueWithAlloc") {
                                        // OK, now we know that the address expression in the placement new
                                        // is a call to setValueWithAlloc, so we can extract the last expression.
                                        // Remember, this is the last expression of the call to _new_, not setValueWithAlloc.
                                        clang::Expr* initializer = cxxNew->getInitializer();
                                        std::string expr = exprToString(initializer, func->getASTContext());
                                        return expr;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return std::string();
}

} // namespace

ClingEngine::ClingEngine(int argc, char **argv)
{
    // Add --ptrcheck to argc, argv
    std::vector<const char*> new_argv;
    for (int i = 0; i < argc; ++i) {
        new_argv.push_back(argv[i]);
    }
    //new_argv.push_back("--ptrcheck");
#ifdef USE_CUDA
    new_argv.push_back("-x");
    new_argv.push_back("cuda");
    new_argv.push_back("--cuda-path=" CUDA_PATH);
    new_argv.push_back("-L");
    new_argv.push_back(CUDA_LIB_DIR);
#endif

    auto start = std::chrono::high_resolution_clock::now();
    argc = new_argv.size();
    interp_ = std::make_unique<cling::Interpreter>(argc, new_argv.data());
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    printf("Interpreter construction took: %ld ms\n", duration.count());
    cling::Interpreter &interp = *interp_;

    // The interpreter has so much internal state going on in order to support incremental parsing,
    // I dont' dare to use it for syntax checking even. Let's create another interpreter for that.
    // We need to pass -fsyntax-only to the compiler.
    // std::vector<const char*> syntax_argv = {argv[0], "-fsyntax-only"};
    // cling::Interpreter syntax(argc, syntax_argv.data());

    // Add the imgui source directory to the include path
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imga");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imgui");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/implot");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/implot3d");
    interp.AddIncludePath(getExecutablePath() + "/../external/imgui/imlatex");
    interp.AddIncludePath(getExecutablePath() + "/../external");
    interp.AddIncludePath(getExecutablePath() + "/../external/nlohmann/json/include");
    // Pre-include it
    std::vector<std::string> headers = {
        "GL/gl.h",
        "GLFW/glfw3.h",
        "imgui.h",
        "imgui_latex.h",
        "implot.h",
        "implot3d.h",
        "cmath",
        "cstdio",
        "algorithm",
        "iostream",
        "imga.h"
    };
    start = std::chrono::high_resolution_clock::now();
    for (const auto& header : headers) {
        auto result = interp.loadHeader(header);
        if (result != cling::Interpreter::kSuccess) {
            std::cerr << "Failed to load header: " << result << std::endl;
            exit(1);
        }
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    printf("Loading headers took: %ld ms\n", duration.count());

    // Tell cling to allow re-definitions
    interp.getRuntimeOptions().AllowRedefinition = true;

    // Normalizer for slide result expressions: the extraction function may
    // yield the callable itself (lambda literal) or a pointer to it (named
    // lambda variable). compileSlide binds each slide's callable through
    // this so both forms end up as a plain copy that can be invoked as f().
    interp.declare("namespace __marcel {\n"
                   "template <class T> auto callable(T v) { return v; }\n"
                   "template <class T> auto callable(T *p) { return *p; }\n"
                   "}\n");
#ifdef USE_CUDA
    start = std::chrono::high_resolution_clock::now();
    auto ptx_interp = interp.getCUDACompiler()->getPTXInterpreter();
    ptx_interp->getRuntimeOptions().AllowRedefinition = true;
    for (const auto& header : headers) {
        auto result = ptx_interp->loadHeader(header);
        if (result != cling::Interpreter::kSuccess) {
            std::cerr << "Failed to load header: " << result << std::endl;
            exit(1);
        }
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    printf("Loading headers into PTX interpreter took: %ld ms\n", duration.count());
#endif
}

ClingEngine::~ClingEngine() = default;

void ClingEngine::compileSetup(SourceFile &setup)
{
    cling::Interpreter &interp = *interp_;

    if (!setup.validated) {
        // May throw — the caller opens the Exception popup, as before.
        cling::InputValidator validator;
        auto result = validator.validate(setup.text());
        setup.setValidated(result == cling::InputValidator::kComplete);
    }

    if (setup.validated && !setup.compiled && !setup.syntax_error) {
        setup.error_markers.clear();
        CaptureStderr cap([&](const char* buf, size_t szbuf) {
            extractMarkers(setup, buf, szbuf);
        });
        interp.getOptions().CompilerOpts.CUDAHost = setup.is_cuda;
        // Setup is compiled at GLOBAL scope via declare(), not process().
        // process() wraps its argument in a function body, which forces any
        // #include in setup.cpp into function scope — fatal for headers that
        // open `extern "C"` / namespace blocks (e.g. librealsense2/rs.hpp).
        // declare() is the same path loadHeader() uses, so setup.cpp can pull
        // in libraries and define globals/typedefs that every slide then sees
        // (slides stay wrapped, so they must not #include). Global-variable
        // initializers in setup run here, which is how setup does one-time
        // work (e.g. starting a camera pipeline).
        auto result = interp.declare(setup.text());
        setup.compiled = true;
        setup.syntax_error = result != cling::Interpreter::kSuccess;

        setup.value.clear();
        setup.function = nullptr;
    }
}

void ClingEngine::compileSlide(SourceFile &slide_src)
{
    cling::Interpreter &interp = *interp_;

    if (!slide_src.validated) {
        // May throw — the caller opens the Exception popup, as before.
        cling::InputValidator validator;
        auto result = validator.validate(slide_src.text());
        slide_src.setValidated(result == cling::InputValidator::kComplete);
    }

    if (slide_src.validated && !slide_src.compiled && !slide_src.syntax_error) {
        slide_src.error_markers.clear();
        CaptureStderr cap([&](const char* buf, size_t szbuf) {
            extractMarkers(slide_src, buf, szbuf, -1);
        });

        // If we disable value printing, we don't have to export symbols from the executable
        // to shared libraries.
        cling::Value V;
        cling::Transaction *transaction = nullptr;
        //auto result = interp.process("void (*update)(ImVec2 slide_size) = [](ImVec2 slide_size){" + slide_src.text() + ";}; update", &V, &transaction, true /* disableValuePrinting */);
        interp.getOptions().CompilerOpts.CUDAHost = slide_src.is_cuda;
        auto result = interp.process(slide_src.text(), &V, &transaction, true /* disableValuePrinting */);

        slide_src.compiled = true;
        if (result != cling::Interpreter::kSuccess) {
            slide_src.syntax_error = true;
        } else {
            slide_src.syntax_error = false;
        }
        // The value in lastV should be a function that we call to re-render the slide
        slide_src.function = nullptr;
        slide_src.value.clear();

        if (V.isValid() && transaction) {
            std::string expr = findResultExprFromExtractionFunction(transaction);

            bool is_record = false;
            void *ptr = nullptr;
            auto T = V.getType().getCanonicalType().getTypePtrOrNull();
            if (const auto *PtrTy = llvm::dyn_cast<clang::PointerType>(T)) {
                const clang::Type *Pointee = PtrTy->getPointeeType().getTypePtr();
                if (const auto *FuncTy = llvm::dyn_cast<clang::FunctionProtoType>(Pointee)) {
                    // It's a function pointer!
                    ptr = V.getPtr();
                    (void)FuncTy;
                }
            } else if (const auto *RefTy = llvm::dyn_cast<clang::ReferenceType>(T)) {
                // Reference type (covers both lvalue and rvalue references)
                const clang::Type *Pointee = RefTy->getPointeeType().getTypePtr();
                if (const auto *CXXRD = Pointee->getAsCXXRecordDecl()) {
                    if (CXXRD->isLambda()) {
                        // This is a reference (or pointer) to a lambda!
                        for (const auto *Method : CXXRD->methods()) {
                            if (Method->getOverloadedOperator() == clang::OO_Call) {
                                // This is the lambda's operator()
                                // Get the lambda object from the reference
                                ptr = V.getPtr();
                            }
                        }
                    }
                }
            } else if (const auto *RecTy = llvm::dyn_cast<clang::RecordType>(T)) {
                // Record type (e.g. a struct or class)
                const clang::CXXRecordDecl *CXXRD = RecTy->getAsCXXRecordDecl();
                if (CXXRD && CXXRD->isLambda()) {
                    // This is a lambda, we can call it
                    for (const auto *Method : CXXRD->methods()) {
                        if (Method->getOverloadedOperator() == clang::OO_Call) {
                            // This is the lambda's operator()
                            // Get the lambda object from the reference
                            is_record = true;
                            ptr = V.getPtr();
                        }
                    }
                }
            }
            if (ptr && !expr.empty()) {
                std::string code;
                if (isPlainName(expr)) {
                    // `expr` is the SOURCE TEXT of the slide's result
                    // expression (e.g. "update"), and its meaning is only
                    // guaranteed right now: with redefinition enabled, the
                    // next slide's compile may shadow the same name. In the
                    // worker, every slide compiles before any slide renders,
                    // so resolving the name lazily at first render made each
                    // slide that returned a variable named `update` execute
                    // the LAST such slide's lambda (slide0 showed slide1).
                    // Bind a per-slide copy of the callable immediately;
                    // rendering calls the copy. (__marcel::callable
                    // normalizes pointer-to-lambda vs value.)
                    std::string alias = "__marcel_slide_fn_";
                    for (char c : slide_src.path.stem().string())
                        alias += (isalnum((unsigned char)c) ? c : '_');
                    std::string bind =
                        fmt::format("auto {} = __marcel::callable(({}));", alias, expr);
                    if (interp.process(bind, nullptr, nullptr,
                                       true /* disableValuePrinting */) ==
                        cling::Interpreter::kSuccess)
                        code = alias + "();";
                    // else: not bindable; fall through to value printing
                } else {
                    // Literal result expression (e.g. a lambda written
                    // directly at the end of the slide). Literals can't be
                    // shadowed by other slides, so re-parsing at render time
                    // is safe here — and it's required: binding a copy of a
                    // literal lambda in one transaction and calling it from
                    // a later one crashes clang's Sema
                    // (DefineImplicitLambdaToFunctionPointerConversion on a
                    // null FunctionDecl).
                    code = fmt::format("(*reinterpret_cast<decltype({})>(0x{:x}))();",
                                       expr, (uintptr_t)ptr);
                    if (is_record) {
                        // decltype of a lambda literal names a distinct
                        // closure type each time; make a sibling object and
                        // take ITS type instead.
                        code = fmt::format("auto similar_lambda = {}; "
                                           "(*reinterpret_cast<decltype(similar_lambda)*>(0x{:x}))();",
                                           expr, (uintptr_t)ptr);
                    }
                }
                if (!code.empty()) {
                    render_tx_[&slide_src] = nullptr;
                    slide_src.function = [this, code, &slide_src]() {
                        cling::Interpreter &interp = *interp_;
                        cling::Value V;
                        cling::Transaction *&tx = render_tx_[&slide_src];
                        if (tx)
                            interp.reevaluate(tx, nullptr);
                        else {
                            CaptureStderr cap([&](const char* buf, size_t szbuf) {
                                extractMarkers(slide_src, buf, szbuf, -1);
                            });
                            auto result = interp.evaluate(code, V, &tx);
                            if (result != cling::Interpreter::kSuccess) {
                                // If we failed to evaluate, kill this function to prevent further calls
                                tx = nullptr;
                                slide_src.function = nullptr;
                            }
                        }
                    };
                }
            }
            if (!slide_src.function) {
                // If we don't have a function, we can still print the value
                llvm::raw_string_ostream os(slide_src.value);
                V.print(os);
                os.flush();
            }
        }
    }
}

void ClingEngine::dump(const char *what, const char *filter)
{
    interp_->dump(what, filter);
}

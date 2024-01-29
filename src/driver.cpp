// Copyright (c) 2022 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Author:  Paul Scheffler <paulsc@iis.ee.ethz.ch>
// Gist:    Main command line driver for SVase.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <CLI/CLI.hpp>

#include "svase/design.h"
#include "svase/diag.h"
#include "svase/preproc.h"
#include "svase/rewrite.h"

#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/driver/Driver.h"
#include "slang/util/TimeTrace.h"

namespace svase {

template <typename Stream, typename String>
void writeToFile(Stream &os, std::string_view fileName, String contents) {
  os.write(contents.data(), contents.size());
  os.flush();
  if (!os)
    throw std::runtime_error(
        fmt::format("Unable to write AST to '{}'", fileName));
}

void writeToFile(std::string_view fileName, std::string_view contents) {
  if (fileName == "-") {
    writeToFile(std::cout, "stdout", contents);
  } else {
    std::ofstream file{std::string(fileName)};
    writeToFile(file, fileName, contents);
  }
}

// TODO: find a nicer way to wrap the timing/exception scopes
int driverMain(int argc, char **argv) {
  Diag diag;
  DiagSev verbosity;
  bool ok;

  CLI::App app{"SVase: a source-to-source SystemVerilog elaborator"};

  // required options (positional)
  std::string top;
  app.add_option("--top,TOP", top, "Top module of design to elaborate")
      ->required();
  std::string outFile;
  app.add_option("--out,OUTPUT", outFile, "The output file (- for stdout)")
      ->required();
  std::vector<std::string> files;
  app.add_option("--files,FILES", files, "The source files to process")
      ->required();
  // Todo: use validators for paths
  // https://cliutils.github.io/CLI11/book/chapters/validators.html

  // additional options
  std::string cliArgfile = "";
  app.add_option("--slang-argfile", cliArgfile,
                 "Argument file overriding Slang default options");
  std::string cliSlangArgs = "";
  app.add_option("--slang-args", cliSlangArgs,
                 "Argument string overriding Slang default options");
  bool cliSplit = false;
  app.add_flag(
      "--split", cliSplit,
      "Write modules into separate file, (Output interpreted as directory)");
  bool cliVersion = false;
  app.add_flag("--version", cliVersion, "Print version information");
  int cliVerbosity = 2;
  app.add_option(
         "-v, --verbosity", cliVerbosity,
         "Verbosity of stderr diagnostics: 1(errors), 2(warnings), 3(notes)")
      ->expected(0, 3);
  bool cliTimeTrace = false;
  app.add_flag("--timetrace", cliTimeTrace,
               "Time each stage and write chrome event trace to JSON");

  CLI11_PARSE(app, argc, argv);

  if (cliTimeTrace)
    TimeTrace::initialize();
  // convert from amount of verbose logging to the level of diagnostics it
  // should show
  verbosity = ((DiagSev)(4 - cliVerbosity));
  diag.setVerbosity(verbosity);

  // Using the Slang driver, see:
  // https://sv-lang.com/classslang_1_1driver_1_1_driver.html
  slang::driver::Driver slangDriver;
  // configure diagnostics
  slangDriver.diagEngine.setIgnoreAllNotes(verbosity > DiagSev::Note);
  slangDriver.diagEngine.setIgnoreAllWarnings(verbosity > DiagSev::Warning);

  // add flags/options to driver
  ok = true;
  slangDriver.addStandardArgs();
  std::string builtinFlags =
      "--ignore-unknown-modules --allow-use-before-declare --single-unit "
      "-Wrange-width-oob -Wrange-oob";

  if (cliArgfile.length() > 0) {
    ok &= slangDriver.processCommandFiles(cliArgfile, true);
  }

  auto slangCmd = fmt::format("{} {} {} --top {} {}", argv[0], cliSlangArgs,
                              builtinFlags, top, fmt::join(files, " "));
  ok &= slangDriver.parseCommandLine(slangCmd);

  ok &= slangDriver.processOptions();
  diag.registerEngine(&slangDriver.sourceManager);
  if (!ok)
    return 2;

  // Preprocess buffers
  /*TypedBumpAllocator<std::string> preBuffers;
  try {
    diag.logStage("PREPROCESS");
    TimeTraceScope timeScope("preproc"sv, ""sv);
     this prints the preprocessed to stdout, not what we want
    I think this never worked, it used to call a test function...
    ok = slangDriver.runPreprocessor(true, false, false);
  } catch (const std::exception &e) {
    diag.log(DiagSev::Fatal, e.what());
    ok = false;
  }
  if (!ok)
    return 3;*/

  // Parse using Slang
  try {
    diag.logStage("PARSE");
    TimeTraceScope timeScope("parse"sv, ""sv);
    ok = slangDriver.parseAllSources();
  } catch (const std::exception &e) {
    diag.log(DiagSev::Fatal, e.what());
    ok = false;
  }
  if (!ok)
    return 4;

  // Compile using Slang
  std::unique_ptr<slang::ast::Compilation> compilation;
  try {
    diag.logStage("COMPILE");
    TimeTraceScope timeScope("compile"sv, ""sv);
    compilation = slangDriver.createCompilation();
    ok = slangDriver.reportCompilation(*compilation, true);

  } catch (const std::exception &e) {
    diag.log(DiagSev::Fatal, e.what());
    ok = false;
  }
  if (!ok)
    return 5;

  // Rewrite sources using our passes
  // TODO: put rewriter-referenced resources in a shared class for convenience
  std::unique_ptr<Design> design;
  std::shared_ptr<SyntaxTree> synTree;
  BumpAllocator alloc;
  TypedBumpAllocator<std::string> strAlloc;
  // try {
  diag.logStage("REWRITE");
  // TimeTraceScope timeScope("rewrite"sv, ""sv);
  design =
      std::make_unique<Design>(*compilation->getRoot().topInstances.begin());
  synTree = compilation->getSyntaxTrees().back();
  // Run our passes (TODO: somehow handle boolean return?)

  // turn each parametrization into a unique module
  synTree =
      UniqueModuleRewriter(*design, alloc, strAlloc, diag).transform(synTree);
  // propagate port-params from instances to new modules (as defaults)
  synTree =
      ParameterRewriter(*design, alloc, strAlloc, diag).transform(synTree);
  // unroll all generate blocks and loops
  synTree = GenerateRewriter(*design, alloc, strAlloc, diag).transform(synTree);
  // propagate components of typedefs (ie other types from pkgs in a struct)
  // to the modules
  synTree = TypedefDeclarationRewriter(*design, alloc, strAlloc, diag)
                .transform(synTree);

  // recompile to make unrolled structure explicit/real
  // (each genblock has a unique location in the source-code)
  diag.logStage("REWRITE [after recompilation]");
  compilation = std::make_unique<Compilation>(compilation->getOptions());
  std::vector<std::pair<std::string, std::string>> intermediateBuffers;
  intermediateBuffers.emplace_back(top, synTree->root().toString());
  Diag newDiag;
  newDiag.setVerbosity(verbosity);
  SourceManager newSourceManager;
  synTree = slang::syntax::SyntaxTree::fromFileInMemory(
      std::string_view(intermediateBuffers.back().second), newSourceManager,
      "after_gen_unfold");
  compilation->addSyntaxTree(synTree);

  design = std::make_unique<Design>(
      *compilation->getRoot().topInstances.begin(), true);
  synTree = compilation->getSyntaxTrees().back();
  newDiag.registerEngine(&newSourceManager);

  // Run passes after unrolling the structure
  // Todo: Is this necessary?
  synTree = UniqueModuleRewriter(*design, alloc, strAlloc, newDiag)
                .transform(synTree);
  // Propagate parameters inside each module (and the unrolled generate blocks)
  synTree =
      ParameterRewriter(*design, alloc, strAlloc, newDiag).transform(synTree);

  // resolve constant continous assignments (assign a = bla;)
  synTree =
      AssignmentRewriter(*design, alloc, strAlloc, newDiag).transform(synTree);
  // } catch (const std::exception e) {diag.log(DiagSev::Fatal, e.what()); ok =
  // false;}
  // if (!ok) return 6;

  // TODO: Postprocess syntax tree into writable buffers, one per root unit
  // member
  std::vector<std::pair<std::string, std::string>> postBuffers;
  std::vector<std::pair<std::string, std::string>> postBuffersFiles;
  try {
    diag.logStage("POSTPROCESS");
    TimeTraceScope timeScope("postproc"sv, ""sv);
    // TODO: proper lib handling
    postBuffers.emplace_back(top, synTree->root().toString());
    int moduleAmount = 0, packageAmount = 0, interfaceAmount = 0,
        classAmount = 0, unknownAmount = 0;
    for (size_t i = 0; i < synTree->root().childNode(0)->getChildCount(); i++) {
      auto child = synTree->root().childNode(0)->childNode(i);
      if (child) {
        if (child->kind == SyntaxKind::ModuleDeclaration) {
          postBuffersFiles.emplace_back(
              fmt::format("{}_{}", "module", moduleAmount++),
              child->toString());
        } else if (child->kind == SyntaxKind::PackageDeclaration) {
          postBuffersFiles.emplace_back(
              fmt::format("{}_{}", "package", packageAmount++),
              child->toString());
        } else if (child->kind == SyntaxKind::InterfaceDeclaration) {
          postBuffersFiles.emplace_back(
              fmt::format("{}_{}", "interface", interfaceAmount++),
              child->toString());
        } else if (child->kind == SyntaxKind::ClassDeclaration) {
          postBuffersFiles.emplace_back(
              fmt::format("{}_{}", "class", classAmount++), child->toString());
        } else {
          postBuffersFiles.emplace_back(
              fmt::format("{}_{}", "unknown", unknownAmount++),
              child->toString());
        }
      }
    }
  } catch (const std::exception &e) {
    diag.log(DiagSev::Fatal, e.what());
    ok = false;
  }
  if (!ok)
    return 7;

  // try {
  diag.logStage("WRITEOUT");
  TimeTraceScope timeScope("writeout"sv, ""sv);
  // TODO: library handling and such; guard that out exists, is legal
  // beforehand!
  writeToFile(outFile, postBuffers.back().second);
  std::string defaultPath = "splitted_output";
  if (cliSplit) {
    if (!std::filesystem::is_directory(defaultPath) ||
        !std::filesystem::exists(defaultPath)) {
      std::filesystem::create_directory(defaultPath);
    }
    for (auto buffer : postBuffersFiles) {
      writeToFile(fmt::format("{}/{}.sv", defaultPath, buffer.first),
                  buffer.second);
    }
  }

  //} catch (const std::exception e) {diag.log(DiagSev::Fatal, e.what()); ok =
  // false;} if (!ok) return 8;

  diag.logStage("DONE");

  // TODO: report on top, unique modules, and blackboxes here.

  // TODO: Timescope implementation!

  return 0;
}

} // namespace svase

int main(int argc, char **argv) { return svase::driverMain(argc, argv); }

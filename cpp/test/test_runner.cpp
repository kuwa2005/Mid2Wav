#include "test_framework.h"
#include <iostream>
#include <string>
#include <vector>

void registerWavTests();
void registerMidiTests();
void registerSoundfontTests();
void registerSynthTests();
void registerFxTests();
void registerConverterTests();
void registerIntegrationTests();

static void printUsage() {
    std::cout << "Usage: test_runner [suite ...]\n"
              << "Suites: wav, midi, soundfont, synth, fx, converter, integration, unit, all\n"
              << "No arguments runs all suites.\n";
}

int main(int argc, char** argv) {
    registerWavTests();
    registerMidiTests();
    registerSoundfontTests();
    registerSynthTests();
    registerFxTests();
    registerConverterTests();
    registerIntegrationTests();

    std::vector<std::string> filters;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "-h" || std::string(argv[i]) == "--help") {
            printUsage();
            return 0;
        }
        filters.push_back(argv[i]);
    }

    TestContext ctx;
    std::cout << "=== Mid2Wav Test Runner ===" << std::endl;

    static const std::vector<std::string> unitSuites = {
        "wav", "midi", "fx"
    };

    for (const auto& suite : allSuites()) {
        bool run = false;
        if (filters.empty()) {
            run = true;
        } else {
            for (const auto& f : filters) {
                if (f == "all") { run = true; break; }
                if (f == "unit") {
                    for (const auto& u : unitSuites) {
                        if (suite.name == u) { run = true; break; }
                    }
                    if (run) break;
                }
                if (suite.name == f) { run = true; break; }
            }
        }
        if (run) runSuite(ctx, suite);
    }

    if (!filters.empty()) {
        bool anyMatched = false;
        for (const auto& f : filters) {
            if (f == "all" || f == "unit") { anyMatched = true; break; }
            for (const auto& suite : allSuites()) {
                if (suite.name == f) { anyMatched = true; break; }
            }
            if (anyMatched) break;
        }
        if (!anyMatched) {
            std::cerr << "Unknown suite(s). ";
            printUsage();
            return 1;
        }
    }

    std::cout << "\n=== Results: " << ctx.passed << " passed, "
              << ctx.failed << " failed, "
              << ctx.skipped << " skipped ===" << std::endl;
    return ctx.failed > 0 ? 1 : 0;
}

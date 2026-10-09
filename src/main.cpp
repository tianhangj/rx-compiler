#include "ast/astbuilder.hpp"
#include "Lexer.h"

#include <exception>
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " [source.rx]\n";
        return 1;
    }

    std::ifstream file;
    if (argc == 2) {
        file.open(argv[1]);
        if (!file) {
            std::cerr << "Cannot open file: " << argv[1] << '\n';
            return 1;
        }
    }

    try {
        antlr4::ANTLRInputStream input(argc == 2 ? file : std::cin);
        rx::Lexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        rx::Parser parser(&tokens);
        auto* tree = parser.crate();
        if (lexer.getNumberOfSyntaxErrors() || parser.getNumberOfSyntaxErrors()) {
            return 1;
        }

        rx::AstBuilder builder;
        auto ast = builder.visitCrate(tree);
        ast->dump();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

#include <exception>
#include <fstream>
#include <iostream>

#include "Lexer.h"

#include "ast/node.hpp"
#include "ast/astbuilder.hpp"
#include "semantic/model.hpp"
#include "semantic/checker.hpp"

int main(int argc, char* argv[]) {
    if (argc != 6 || strcmp(argv[1], "--stage") || strcmp(argv[4], "-o")) {
        std::cerr << "usage: " << argv[0] << " --stage <stage> <source> -o <output>\n";
        return 1;
    }
    std::string stage = argv[2];
    std::string source = argv[3];
    std::string output = argv[5];

    std::ifstream file;
    if (!source.empty() && source != "-") {
        file.open(source);
        if (!file) {
            std::cerr << "Cannot open file: " << source << '\n';
            return 1;
        }
    }

    try {
        antlr4::ANTLRInputStream input(file.is_open() ? file : std::cin);
        rx::Lexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        rx::Parser parser(&tokens);
        auto* tree = parser.crate();
        if (lexer.getNumberOfSyntaxErrors() || parser.getNumberOfSyntaxErrors()) {
            return 1;
        }

        rx::AstBuilder builder;
        auto ast = builder.visitCrate(tree);
        if (stage == "ast") {
            ast->dump();
            return 0;
        }
        rx::SemanticModel model = rx::checkSemantics(ast.get());
        if (stage == "semantic") {
            return 0;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

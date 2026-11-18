/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <google/protobuf/compiler/command_line_interface.h>
#include <google/protobuf/compiler/cpp/generator.h>

int main(int argc, char **argv)
{
    google::protobuf::compiler::CommandLineInterface cli;
    google::protobuf::compiler::cpp::CppGenerator cpp;
    cli.RegisterGenerator("--cpp_out", &cpp, "Generate C++ source and headers.");
    return cli.Run(argc, argv);
}

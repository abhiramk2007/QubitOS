#ifndef QUBITOS_QASM_PARSER_HPP
#define QUBITOS_QASM_PARSER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "dag.hpp"

namespace qubitos {

struct ParsedCircuit {
    std::string filename;
    size_t num_qubits = 0;
    CircuitDAG dag;
    std::vector<std::vector<size_t>> parallel_layers;
};

class QasmParser {
public:
    // Parses an OpenQASM 2.0 file from disk.
    // Throws std::runtime_error with a clear diagnostic if parsing fails.
    static ParsedCircuit parse_file(const std::string& filepath);

    // Parses OpenQASM 2.0 source code from a string (useful for testing and memory parsing).
    static ParsedCircuit parse_string(const std::string& content, const std::string& source_name = "<inline>");

private:
    struct RegisterInfo {
        size_t base_index;
        size_t size;
    };

    static std::string strip_comments(const std::string& text);
    static std::vector<std::string> split_statements(const std::string& text);
    static std::string trim(const std::string& s);
    static size_t parse_qubit_operand(const std::string& op_str, 
                                      const std::unordered_map<std::string, RegisterInfo>& registers);
};

} // namespace qubitos

#endif // QUBITOS_QASM_PARSER_HPP


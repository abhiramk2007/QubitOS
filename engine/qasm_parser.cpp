#include "qasm_parser.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace qubitos {

std::string QasmParser::trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

std::string QasmParser::strip_comments(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        size_t comment_pos = line.find("//");
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        result += line + "\n";
    }
    return result;
}

std::vector<std::string> QasmParser::split_statements(const std::string& text) {
    std::vector<std::string> stmts;
    std::string current;
    for (char c : text) {
        if (c == ';') {
            std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                stmts.push_back(trimmed);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    std::string remaining = trim(current);
    if (!remaining.empty()) {
        throw std::runtime_error("Syntax error: statement not terminated by semicolon: '" + remaining + "'");
    }
    return stmts;
}

size_t QasmParser::parse_qubit_operand(const std::string& op_str,
                                       const std::unordered_map<std::string, RegisterInfo>& registers) {
    std::string s = trim(op_str);
    size_t bracket_open = s.find('[');
    size_t bracket_close = s.find(']');
    if (bracket_open == std::string::npos || bracket_close == std::string::npos || bracket_close < bracket_open) {
        throw std::runtime_error("Malformed qubit operand: '" + s + "'");
    }

    std::string reg_name = trim(s.substr(0, bracket_open));
    std::string idx_str = trim(s.substr(bracket_open + 1, bracket_close - bracket_open - 1));

    auto it = registers.find(reg_name);
    if (it == registers.end()) {
        throw std::runtime_error("Reference to undeclared quantum register: '" + reg_name + "'");
    }

    size_t idx = 0;
    try {
        idx = std::stoul(idx_str);
    } catch (...) {
        throw std::runtime_error("Invalid index in qubit operand: '" + s + "'");
    }

    if (idx >= it->second.size) {
        throw std::runtime_error("Qubit index out of bounds: " + s + " (register size: " + 
                                 std::to_string(it->second.size) + ")");
    }

    return it->second.base_index + idx;
}

ParsedCircuit QasmParser::parse_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_string(buffer.str(), filepath);
}

ParsedCircuit QasmParser::parse_string(const std::string& content, const std::string& source_name) {
    ParsedCircuit circuit;
    circuit.filename = source_name;

    std::string clean_text = strip_comments(content);
    std::vector<std::string> statements = split_statements(clean_text);

    std::unordered_map<std::string, RegisterInfo> registers;
    size_t total_qubits = 0;

    for (const auto& stmt : statements) {
        std::string s = trim(stmt);
        if (s.empty()) continue;

        // Ignore OPENQASM header
        if (s.rfind("OPENQASM", 0) == 0) continue;
        // Ignore include statements
        if (s.rfind("include", 0) == 0) continue;
        // Ignore classical registers
        if (s.rfind("creg", 0) == 0) continue;
        // Ignore barriers, measurements, and resets
        if (s.rfind("barrier", 0) == 0) continue;
        if (s.rfind("measure", 0) == 0) continue;
        if (s.rfind("reset", 0) == 0) continue;

        // Parse quantum register declaration: qreg name[size];
        if (s.rfind("qreg", 0) == 0) {
            std::string decl = trim(s.substr(4));
            size_t b_open = decl.find('[');
            size_t b_close = decl.find(']');
            if (b_open == std::string::npos || b_close == std::string::npos || b_close < b_open) {
                throw std::runtime_error("Malformed qreg declaration: '" + s + "' in " + source_name);
            }
            std::string reg_name = trim(decl.substr(0, b_open));
            std::string sz_str = trim(decl.substr(b_open + 1, b_close - b_open - 1));
            size_t sz = 0;
            try {
                sz = std::stoul(sz_str);
            } catch (...) {
                throw std::runtime_error("Invalid size in qreg: '" + s + "' in " + source_name);
            }
            if (registers.find(reg_name) != registers.end()) {
                throw std::runtime_error("Duplicate qreg declaration: '" + reg_name + "' in " + source_name);
            }
            registers[reg_name] = {total_qubits, sz};
            total_qubits += sz;
            continue;
        }

        // Parse quantum gate line:
        // Format: gate_name[(param1, ...)] q0, q1, ...
        size_t pos = 0;
        // Find end of gate name or start of parameter list
        while (pos < s.size() && !std::isspace(static_cast<unsigned char>(s[pos])) && s[pos] != '(') {
            pos++;
        }
        std::string gate_name = s.substr(0, pos);

        // If parameters are present, skip to matching ')'
        if (pos < s.size() && s[pos] == '(') {
            size_t paren_depth = 1;
            pos++;
            while (pos < s.size() && paren_depth > 0) {
                if (s[pos] == '(') paren_depth++;
                else if (s[pos] == ')') paren_depth--;
                pos++;
            }
            if (paren_depth != 0) {
                throw std::runtime_error("Unmatched parenthesis in gate parameters: '" + s + "' in " + source_name);
            }
        }

        // Remainder of string contains comma-separated qubit operands
        std::string operands_str = trim(s.substr(pos));
        if (operands_str.empty()) {
            throw std::runtime_error("Gate '" + gate_name + "' missing qubit operands in " + source_name);
        }

        std::vector<size_t> gate_qubits;
        std::stringstream ss(operands_str);
        std::string token;
        while (std::getline(ss, token, ',')) {
            std::string op = trim(token);
            if (!op.empty()) {
                gate_qubits.push_back(parse_qubit_operand(op, registers));
            }
        }

        if (gate_qubits.empty()) {
            throw std::runtime_error("No qubit targets parsed for gate: '" + s + "' in " + source_name);
        }

        if (gate_qubits.size() > 3) {
            throw std::runtime_error("Gates with >3 qubits not supported: '" + s + "' in " + source_name);
        }

        circuit.dag.add_gate(gate_name, gate_qubits);
    }

    circuit.num_qubits = total_qubits;
    // Validate the DAG and produce parallel topological layers
    circuit.parallel_layers = circuit.dag.run_kahns_algorithm();

    return circuit;
}

} // namespace qubitos


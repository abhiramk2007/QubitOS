#!/usr/bin/env python3
"""
tools/export_hardware.py

OPTIONAL UTILITY: Export IBM Quantum Fake Backend to QubitOS hardware.json
========================================================================
DISCLAIMER:
This script is completely OPTIONAL and provided for offline convenience.
It is UNTESTED against the user's specific Qiskit version and environment.
QubitOS runtime engine and Web UI NEVER require Python or Qiskit to run;
they consume standard hardware.json directly.

Usage:
    python3 tools/export_hardware.py [backend_name] [output_file]
Example:
    python3 tools/export_hardware.py FakeManilaV2 hardware.json
"""

import sys
import json

def export_backend(backend_name="FakeManilaV2", output_path="hardware.json"):
    print(f"[QubitOS Hardware Exporter] Attempting to import Qiskit fake backend: {backend_name}...")
    try:
        # Support Qiskit 1.x and legacy qiskit_ibm_runtime fake backends
        backend = None
        try:
            import qiskit_ibm_runtime.fake_provider as fake
            if hasattr(fake, backend_name):
                backend = getattr(fake, backend_name)()
        except ImportError:
            pass

        if backend is None:
            try:
                import qiskit.providers.fake_provider as fake
                if hasattr(fake, backend_name):
                    backend = getattr(fake, backend_name)()
            except ImportError:
                pass

        if backend is None:
            raise RuntimeError(f"Could not locate backend class '{backend_name}' in Qiskit fake providers.")

    except ImportError as e:
        print("\n[ERROR] Qiskit is not installed in this Python environment.")
        print("To install Qiskit: pip install qiskit qiskit-ibm-runtime")
        print("Remember: QubitOS does not require Python or Qiskit; you can use the default illustrative hardware.json.")
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERROR] Failed to load fake backend '{backend_name}': {e}")
        print("Remember: QubitOS does not require Python or Qiskit; you can use the default illustrative hardware.json.")
        sys.exit(1)

    print(f"[QubitOS Hardware Exporter] Loaded backend: {backend.name} (qubits: {backend.num_qubits})")

    edges = []
    seen_edges = set()

    # Extract 2-qubit gate error rates from target or properties
    if hasattr(backend, "target"):
        target = backend.target
        for inst_name, qargs_map in target.items():
            for qargs, prop in qargs_map.items():
                if len(qargs) == 2 and prop is not None and prop.error is not None:
                    u, v = qargs
                    edge_key = (min(u, v), max(u, v))
                    if edge_key not in seen_edges:
                        seen_edges.add(edge_key)
                        edges.append({
                            "a": edge_key[0],
                            "b": edge_key[1],
                            "error": round(float(prop.error), 6)
                        })
    elif hasattr(backend, "properties") and backend.properties() is not None:
        props = backend.properties()
        for gate in props.gates:
            if len(gate.qubits) == 2:
                u, v = gate.qubits
                edge_key = (min(u, v), max(u, v))
                if edge_key not in seen_edges:
                    seen_edges.add(edge_key)
                    # Extract gate error parameter
                    err = 0.01  # default fallback
                    for param in gate.parameters:
                        if param.name == "gate_error":
                            err = float(param.value)
                            break
                    edges.append({
                        "a": edge_key[0],
                        "b": edge_key[1],
                        "error": round(err, 6)
                    })

    # Sort edges for deterministic presentation
    edges.sort(key=lambda e: (e["a"], e["b"]))

    # Extract per-qubit physical properties (T1, T2, readout error) if available
    qubit_props = []
    if hasattr(backend, "properties") and backend.properties() is not None:
        props = backend.properties()
        for q in range(backend.num_qubits):
            q_info = {"id": q}
            try:
                q_info["t1_us"] = round(props.t1(q) * 1e6, 2)
            except Exception:
                pass
            try:
                q_info["t2_us"] = round(props.t2(q) * 1e6, 2)
            except Exception:
                pass
            try:
                q_info["readout_error"] = round(props.readout_error(q), 5)
            except Exception:
                pass
            qubit_props.append(q_info)

    hardware_dict = {
        "name": f"{backend.name} (exported from Qiskit fake backend)",
        "num_qubits": backend.num_qubits,
        "edges": edges,
        "qubit_properties": qubit_props
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(hardware_dict, f, indent=2)

    print(f"[SUCCESS] Exported {len(edges)} edges for {backend.num_qubits} qubits to '{output_path}'.")

if __name__ == "__main__":
    b_name = sys.argv[1] if len(sys.argv) > 1 else "FakeManilaV2"
    out_file = sys.argv[2] if len(sys.argv) > 2 else "hardware.json"
    export_backend(b_name, out_file)


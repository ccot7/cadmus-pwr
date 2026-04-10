# CadmusPwr <img src="assets/logo.png" width="50" align="right">

**CadmusPwr** is a lightweight, low-level power monitoring utility for macOS and Linux. It serves as a high-performance alternative to the Intel Power Gadget, providing real-time insights into CPU power consumption, frequency, and thermal metrics.

Inspired by Intel Power Gadget & named after **Cadmus**, the legendary founder of Tyre, this tool is designed for developers and power users who need precise hardware telemetry without the overhead of heavy GUI suites.

---

## 🛠 Features
- **Cross-Platform:** Native implementations for both macOS (Swift) and Linux (C/GTK3).
- **Low Overhead:** Optimized to ensure that the act of monitoring doesn't impact the power metrics being read.
- **Hardware Agnostic:** Designed to interface with Intel RAPL (Running Average Power Limit) and Apple Silicon `powermetrics`.

---

## 📂 Repository Structure

This repository is organized by platform. Please refer to the specific subdirectory for build and installation instructions:

- **[macOS version](./macos)**  
  Built with Swift and Xcode. Features a sleek HUD-style interface and utilizes `powermetrics` via a custom sudoers configuration.
  
- **[Linux version](./linux)**  
  Built with C and GTK3. Interfaces directly with the Linux `intel_rapl` driver for raw power data.

---

## ⚖️ License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for more information.

---

## 👤 Author
**Cadmus of Tyre**  
GitHub: [@ccot7](https://github.com/ccot7)

---
> *The sentinel for your CPU's power, thermals, and performance limits.*

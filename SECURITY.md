# Security Policy

Since `easy_stack` is a lightweight memory allocator (frequently deployed in bare-metal, embedded, or performance-critical environments), bugs related to buffer overflows, LIFO-integrity violations, or metadata corruption are treated as serious security and stability issues.

## Supported Versions

Currently, only the `main` branch and the active development releases (v1.0.x) are supported with security updates. 

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0.0 | :x:                |

## Reporting a Vulnerability

If you have discovered a vulnerability, memory corruption bug, or bypass of the internal safety checks:

1. **Open a public GitHub Issue** in this repository.
2. Provide a brief description of the bug and how it was triggered.
3. Include a minimal reproducible C snippet or a `libFuzzer` crash dump.

Reported vulnerabilities are actively monitored, and patches will be released as quickly as possible.
# Security Policy

## Supported versions

Core Pilot is under active pre-release development. Security fixes are provided for the latest published release candidate only.

| Version | Supported |
| --- | --- |
| Latest release candidate | Yes |
| Earlier releases | No |

## Reporting a vulnerability

Report suspected vulnerabilities through [GitHub Private Vulnerability Reporting](https://github.com/anapeksha/core-pilot/security/advisories/new). Do not open a public issue or discussion, and do not include credentials, setup payloads, private keys, device seeds, session tokens, network captures containing secrets, or other sensitive data in public channels.

Please include:

- The affected version or commit.
- The affected hardware and network configuration, when relevant.
- Reproduction steps or a minimal proof of concept.
- The expected and observed behavior.
- The likely impact and any known mitigations.

You should receive an acknowledgement within three business days. After triage, maintainers will share the assessment and coordinate remediation and disclosure through the private advisory. Timelines depend on severity, hardware requirements, and the availability of a safe fix.

## Research guidelines

Good-faith research should avoid harming people, devices, or networks. Test only systems you own or are explicitly authorized to assess. Do not access other users' data, disrupt deployed devices, weaken production security controls, expose a Core Pilot instance to the public internet, or retain sensitive information beyond what is necessary to report the issue.

Core Pilot has not completed a formal security audit or Matter certification. Security-sensitive deployment decisions should account for the limitations documented in the latest release notes.

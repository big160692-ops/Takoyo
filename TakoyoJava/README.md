# Takoyo (Java edition)

A Swing-based Java client for Takoyo. Talks to any OpenAI-compatible endpoint
(the bundled Takoyo engine, OmniRoute, OpenAI, Ollama, ...).

## Build & run

Requires JDK 11+.

```bash
cd TakoyoJava
javac Takoyo.java
java Takoyo
```

Configure endpoint, API key, model, and working folder via the gear button.
Files the assistant creates are saved automatically to the working folder
(defaults to your home directory), and every code block has a Save button.
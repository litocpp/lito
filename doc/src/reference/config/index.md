# Configuration Reference

Configuration controls project-local integration policy that does not belong to package identity:
tool paths, standard library selection, global C/C++/link options, search roots, local source
patches, lock location, install root, and documentation tool location.

There are three input forms:

- committed project configuration: `lito-config.toml`;
- machine-local persisted configuration: `.lito/config.toml`;
- invocation overrides: repeated `-c/--config KEY=VALUE` assignments.

Unknown configuration keys are errors. Paths are resolved by the config owner relative to the
project root, not the current shell after project discovery.

Read [Precedence and locations](precedence-and-locations.md), then use the complete
[Configuration keys](keys.md) list.

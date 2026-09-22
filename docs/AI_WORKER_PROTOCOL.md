# AI worker protocol

This repository participates in the HillStreet multi-agent development workflow for ChatGPT Work, Claude Cowork/Code, Grok, Codex, and other approved workers.

Canonical protocol:

- [Multi-agent development and autonomous delivery protocol](https://github.com/hillstreet-ph/open-connect/blob/chore/open-platform-backend/docs/MULTI_AGENT_DEVELOPMENT_PROTOCOL.md)
- [Infrastructure blueprint](https://github.com/hillstreet-ph/open-connect/blob/chore/open-platform-backend/docs/INFRASTRUCTURE_BLUEPRINT.md)
- [AI agent handoff prompt](https://github.com/hillstreet-ph/open-connect/blob/chore/open-platform-backend/docs/AI_AGENT_HANDOFF_PROMPT.md)

Before changing `open-tgate`, read its local `AGENTS.md`, README, contribution guide, workflows, migrations, open issues, and open pull requests. Local repository instructions take precedence when they are more specific.

Mandatory behavior:

1. Search before creating. Extend or repair existing code first.
2. Use one GitHub issue, one task lock, and one issue-scoped branch.
3. Do not duplicate an active issue, PR, component, workflow, service, schema, or deployment.
4. Make the smallest safe change and preserve existing architecture.
5. Run the repository's real tests and applicable container, migration, security, and E2E checks.
6. Never commit or print credentials. Google Sheets contains inventory references only.
7. Merge only after required checks and review gates pass.
8. Release immutable versions and Docker digests.
9. Deploy to staging, verify health, then promote or roll back.
10. Record final evidence in GitHub.

A green build is not proof of a healthy production deployment.

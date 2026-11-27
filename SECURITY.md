# Security

## Supported versions

Composer is in beta: only the latest release gets fixes.

## Reporting a vulnerability

Please do not open a public issue for a security problem.
Report it through the repository's private report form: <https://github.com/kulbiq/agogica-composer/security/advisories/new> (the "Security" tab, "Report a vulnerability").
Only the maintainers can read what is sent there.

Say which version, plugin format, host and system you used, and how to reproduce the problem.
You can expect an answer within a week, and a fix or a plan in the following release.

## What counts

Composer is an audio plugin that runs inside other programs and loads project files, so these matter most:
- a project file or automation data that crashes the host or corrupts memory;
- the editor's page reaching anything outside the plugin's own embedded files;
- anything sent over the network that `PRIVACY.md` does not describe.

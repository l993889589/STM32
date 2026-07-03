# Public Release Checklist

Before pushing this repository publicly:

- [ ] Review all project folders for company/private/customer-specific content.
- [ ] Review Wi-Fi, MQTT, server, SIM, ICCID, IMEI, token, key, and password-like strings.
- [ ] Confirm third-party code licenses allow redistribution.
- [ ] Confirm vendor firmware blobs and PDF/manual files are not included unless redistribution is allowed.
- [ ] Decide whether to keep this as one monorepo or split `ldc`, `STM32H563`, and `EC20` into separate repositories.
- [ ] Add hardware photos or diagrams only if they are yours or redistribution is allowed.
- [ ] Build at least the primary Keil projects after cleanup.
- [ ] Add a release note describing which board and toolchain versions were tested.

Recommended first GitHub commits:

1. `docs: add public repository overview`
2. `chore: add curated STM32 source tree`
3. `docs: document migration from local and Gitee history`
4. `docs: add project index and publication checklist`

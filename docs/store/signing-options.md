# Public code signing options for Numpad9

Primary sources checked on **October 13, 2026**. No provider application,
purchase, account creation, credential creation or message was submitted during
this research. These notes concern the current Windows EXE installer and native
TSF DLLs. Having a Microsoft developer account does not provide their public
Authenticode signatures automatically.

The verified free option is SignPath Foundation, subject to project acceptance.
A new public GPL repository alone does not establish eligibility. If an
immediate release is required, a publicly trusted commercial certificate is a
separate option; its price, identity verification and key access must be checked
before ordering. A country in a billing profile, development machine timezone,
repository profile or interface language is not proof of legal eligibility.

## Microsoft Store requirements and documentation ambiguity

The [specific MSI/EXE package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements)
require the installer and all its PE files to have code-signing certificates
chaining to a CA in Microsoft's Trusted Root Program. They also require a fixed
versioned HTTPS installer URL, silent installation and a standalone installer.
This means the application EXE, x64/x86 IME DLLs and generated uninstaller all
need valid signatures; signing only the outer download is insufficient.

The [code-signing overview](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options),
updated August 29, 2026, labels Azure Artifact Signing, OV and EV certificates
“Store eligible: No” in its comparison table. However, the same article explains
that Store EXE/MSI submissions require a Trusted Root Program CA certificate.
Its introduction says the other choices discuss distribution outside the Store.
The table therefore cannot reasonably establish a categorical ban on all CA
certificates for EXE submissions. Treat it as ambiguous guidance, and confirm
the intended certificate with Microsoft before paying or making compatibility
claims. Public root-chain compliance is necessary; it does not guarantee app
certification or acceptance of a particular publisher arrangement.

Store MSIX submissions generally receive Microsoft's signature automatically,
according to that overview. Converting this installer into an MSIX is a separate
engineering decision: the current app registers native TSF components for other
processes. This research does not establish that such registration would work
through a Store MSIX package, so MSIX must not be presented as a completed
certificate workaround for the current app.

## SignPath Foundation: free, with project review

[SignPath Foundation](https://signpath.org/) offers qualifying OSS projects
free managed signing. Its HSM retains the key, so the maintainer does not need a
USB token, downloadable PFX or their own public certificate. A Mac can be the
maintainer's workstation while Windows builds run on GitHub-hosted runners;
the [GitHub integration](https://docs.signpath.io/trusted-build-systems/github)
verifies build origin and requires workflow artifacts uploaded before signing.
For OSS projects all preceding workflow jobs must use GitHub-hosted agents.

The [Foundation terms](https://signpath.org/terms.html) require an actively
maintained, documented, already released OSS project and verifiable reputation
for executable applications. Acceptance is discretionary; no minimum age,
stars or promised review duration is published there. Required practices
include OSI-approved licensing without proprietary components or commercial
dual licensing, repository ownership, MFA, clear author/reviewer/approver roles,
manual approval of each release, verifiable builds, consistent product/version
metadata, privacy documentation, announced system changes and uninstallation.
The home/release pages must publish a code-signing policy and provider credit.

The certificate identifies **SignPath Foundation**, not the individual developer.
Do not claim sponsorship before acceptance. For Numpad9, submit the GPL source,
build workflow, public release and explain that its separately licensed
CC-CEDICT/SCOWL files are dictionary content. Confirm that the content licenses
and generated Inno uninstaller are acceptable under Foundation policy.

Practical sequence if selected:

1. Publish the documented, buildable project and an actual release with visible
   repository ownership; enable GitHub MFA and establish signing roles.
2. The maintainer applies through the [official application page](https://signpath.org/apply.html),
   providing project/release evidence. Acceptance and timing remain external.
3. After acceptance, obtain the organization ID, project/policy identifiers and
   submitter token; configure the protected CI workflow and manual approvals.
4. Sign all native payloads, generate and sign the uninstaller, then build/sign
   the final installer. Verify every installed PE and exact installer checksum.

Current local-certificate signing scripts are not a ready SignPath adapter.
Foundation keys cannot be passed to a local certificate-store signing command.
Also, the release validator currently matches the certificate's simple name
against the application publisher. A Foundation integration must distinguish
the legal application publisher from the explicitly allowed signing identity,
while preserving root-chain and timestamp verification. Setting the Store
publisher to the Foundation merely to satisfy a local check is inappropriate.

The [Inno Setup SignedUninstaller documentation](https://jrsoftware.org/ishelp/topic_setup_signeduninstaller.htm)
describes a reusable signed-uninstaller file and the
[SignTool callback](https://jrsoftware.org/ishelp/topic_setup_signtool.htm) can
sign Setup, Uninstall and selected source files. A cloud adapter or staged
signed-uninstaller workflow needs implementation and verification; a final
installer signature does not sign the embedded payloads automatically.

## Azure Artifact Signing: inexpensive, geographically restricted

The [current quickstart](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart),
updated May 21, 2026, permits **Public Trust** identities for organizations in
the US, Canada, EU, UK, Australia, New Zealand, Japan, South Korea, Singapore,
Switzerland, Norway and Israel. Individuals must be located in the US or Canada.
If the developer's actual verified location is China, that list does not provide
a Public Trust route. Choose based on the real legal identity, not a selected
Azure deployment region. Individual billing-account details must match the
government ID; an Azure subscription and Entra tenant are required.

The [trust-model documentation](https://learn.microsoft.com/en-us/azure/artifact-signing/concept-trust-models),
updated January 8, 2026, states that Public Trust uses Microsoft Identity
Verification Root Certificate Authority 2020, included in Microsoft's root
program for code signing and timestamping. It supports Win32 Authenticode.
Private Trust is not publicly trusted; Public Trust Test is also not a substitute
for a public release signature. These facts support the required root-chain
property, but the Store overview's conflicting table still warrants confirmation.

The overview quotes approximately **US $9.99/month**. The
[pricing page](https://azure.microsoft.com/en-us/pricing/details/artifact-signing/)
lists 5,000 signatures/month for Basic; its dynamically rendered prices were
unavailable in the text view. Billing starts when the signing account is created
and is not prorated. Verify the actual regional price before creation. Keys are
managed remotely; no personal token is required. A Windows CI signing job can
be controlled from a Mac.

## Certum OSS: low advertised price, currently unavailable in its shop

[Certum Open Source Code Signing in the Cloud](https://shop.certum.eu/open-source-code-signing-on-simplysign.html)
advertises a starting price of **€49** but showed **out of stock** when checked.
This is not a free certificate and not an immediately confirmed purchase route.
Its cloud key avoids a card/reader, supports Windows executables and macOS
signing tools, and requires SimplySign mobile/desktop applications and internet.
The certificate uses an “Open Source Developer” identity prefix.

The [identity requirements](https://support.certum.eu/en/code-signing-required-documents/)
call for individual identity verification, a utility bill and a publicly
available ongoing OSS project whose relationship to the subscriber is clear.
The OSS product is individual-only, and Certum can revoke it if used to sign
commercially distributed software. A paid Store app should therefore not assume
eligibility for this product. These pages do not establish approval of the
specific developer's country/documents; that must be verified with the issuer.

The [standard cloud product](https://shop.certum.eu/standard-code-signing-in-the-cloud.html?___store=certum_en)
advertises a starting price of €209 and also showed out of stock. It supports
individual or organization identities. Recheck vendor stock and eligibility
before treating either product as a deliverable signing route.

## SSL.com individual certificate: concrete commercial alternative

[SSL.com IV Code Signing](https://www.ssl.com/products/software-integrity/code-signing/iv/)
lists **US $129/year** for a one-year certificate and permits individuals without
business registration, with government-ID validation. The private key must be
held in compliant hardware or its cloud signing service. This certificate price
does not include a usable key-storage/signing arrangement by itself.

[eSigner for Code](https://www.ssl.com/products/software-integrity/signing-service/)
is a separate cloud subscription. Its default annual presentation showed Tier 1
at $15/month equivalent with 240 signatures, and new certificates receive
30 days of cloud signing before subscription charges. This is a trial of the
signing service, not a free public certificate. Confirm billing period,
country/identity eligibility and complete price at checkout; the pages do not
guarantee acceptance of a particular China-based applicant. Cloud signing can
integrate with CI without a personal hardware token.

This is a viable vendor channel to investigate for a new individual project
because it does not publish the Foundation's community-reputation prerequisite.
No ordering or eligibility request has been performed. Among the primary vendor
sources checked, no additional universally available free Authenticode program
for brand-new OSS executables was verified.

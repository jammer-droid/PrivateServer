---
name: document-subsystem
description: Create, refresh, or review one explicitly selected subsystem wiki area as publishable Korean technical documentation grounded in current repository code and tests. Use only when the user explicitly invokes $document-subsystem for a public boundary, runtime scenario, ownership/lifetime design, shutdown flow, source map, or an optional diagram review tied to that wiki area. Registered public documents must exclude internal measurement data and evaluation rubrics.
---

# Document Subsystem

Write one reader-facing subsystem wiki slice from the repository state selected by the user. Treat the wiki as the deliverable: do not mix tracker operations, portfolio coaching, learning assessment, or internal audit narration into the published document.

## Authority and scope

- Treat explanation and review requests as read-only.
- Treat an explicit create, update, or refresh request as authorization to edit only the selected `wiki/<subsystem>/` area and its README index when adding a document.
- Read code, tests, ADRs, design documents, evidence, and supplied tracker context as sources. Do not modify them unless the user separately expands scope.
- Treat `.wiki-documents` as the publication whitelist. Every registered Markdown file must be safe to publish as-is; unregistered notes and evidence remain private source material.
- Do not create or update tracker items.
- Work on exactly one coherent reader question per invocation.
- Preserve unrelated work. Report source/document drift in the response instead of writing internal cleanup notes into the wiki.

## Workflow

### 1. Resolve the invocation

Interpret the invocation as:

```text
$document-subsystem <subsystem> [area]
```

If `area` is absent, inspect the subsystem, present only evidence-supported candidate areas, and let the user select one. Use `$grilling` only when a material editorial or design branch remains; do not ask for repository facts that can be inspected.

### 2. Ensure the wiki entrypoint

For an authorized create or update request, ensure the common wiki skeleton exists:

```bash
python3 .agents/skills/document-subsystem/scripts/bootstrap_wiki.py \
  --repo-root . \
  --project-name "<project name>" \
  --subsystem <subsystem> \
  --title "<subsystem title>"
```

The bootstrap creates only missing files and never overwrites existing wiki content. It also creates `wiki/<subsystem>/.wiki-documents` with the subsystem README as the first managed public document. For a read-only request, report a missing entrypoint without creating it.

### 3. Load current evidence

Read, in order:

1. applicable `AGENTS.md` files and domain context;
2. `wiki/README.md`, the selected `.wiki-documents`, and the selected subsystem index; when authoring support files are absent from `wiki/`, use the bundled copies under `.agents/skills/document-subsystem/assets/wiki/`;
3. the selected-area document when present;
4. the matching skill reference when one exists;
5. relevant code, project files, tests, ADRs, detailed design, and evidence.

For `network-runtime`, read [references/network-runtime.md](references/network-runtime.md). References route investigation; current code, project files, tests, and artifacts win when they disagree.

Record `git rev-parse HEAD`. Use `Baseline: Not established` only when the document intentionally describes an unfixed draft.

### 4. Frame one wiki slice

Before editing, state:

- subsystem and area;
- intended reader and question answered;
- included and excluded scope;
- source and evidence paths to verify;
- output file and validation path.

Prefer the smallest document that explains one stable boundary or runtime scenario.

### 5. Write for the public reader

Use the nearest template under `wiki/templates/`, falling back to `.agents/skills/document-subsystem/assets/wiki/templates/`. Write in Korean and preserve code identifiers or technical terms in English when translation reduces precision.

Keep these metadata fields at the top:

```text
Document status: Draft | Reviewed
Baseline: <commit, tag, or Not established>
Last reviewed: <YYYY-MM-DD or Not reviewed>
```

Apply these rules:

- Lead with the reader's question, responsibility, dependency direction, interface, invariant, ownership, and lifetime.
- Explain engineering rationale as part of the design, not as a `포트폴리오 가치` section.
- Include only reader-relevant current scope and constraints. Use `지원 범위와 제약` when needed.
- Keep code/document drift, migration cleanup, publication blockers, tracker state, and next-task routing out of the wiki; report them separately in the response.
- Do not narrate the documentation workflow, agent actions, current session, or Knowledge Gate.
- Use code and behavioral tests as private authoring evidence, then publish only the reader-facing design, contract, scenario, limitation, and source/test navigation.
- Do not claim a current test/smoke pass from file existence alone.
- Omit private tracker URLs, local absolute paths, credentials, personal learning records, and internal-only notes.
- Omit internal measurement values and evaluation material: benchmark or test counts, pass rates, client/load counts, latency/throughput/memory values, run IDs, machine details, diagnostic thresholds, verdict fields, acceptance criteria, scorecards, and evidence-level rubrics.
- Treat unit-bearing operational values conservatively. In public prose, prefer a named configuration or contract symbol plus its public source link over a literal memory, timing, capacity, or rate value.
- Do not register benchmark reports or evidence matrices. Keep raw measurements and internal evaluation records outside the public whitelist until the user separately curates a public result.
- Do not use `planned`, `implemented`, or `verified` as document status.
- Do not invent future architecture or present an unmeasured optimization as a result.

When adding a document, update only the selected subsystem README index and add the new subsystem-relative Markdown path to `.wiki-documents`. Register only documents that already satisfy the public-content exclusions above; do not register personal notes, raw evidence, benchmark reports, or ad hoc Markdown. A registered document must not link to an unregistered Markdown file inside `wiki/`.

### 6. Review a diagram when requested

Diagrams are optional. Add or review one only when the user requests it and it materially clarifies the selected reader question.

- Prefer one editable source per wiki document. Use multiple frames in that source only for complementary views of the same question.
- Prefer `wiki/<subsystem>/diagrams/<document-slug>.excalidraw` as the editable source and a matching `.svg` as the public export.
- When the user supplies an Excalidraw file, inspect it without modifying it unless the user explicitly authorizes diagram edits.
- Compare labels, arrow semantics, ownership, build-time/runtime distinctions, and grouping against current project files and code.
- Treat visual groupings as explanatory responsibility clusters unless the repository proves physical modules or runtime components.
- Do not depict a static library as a separately running component after its object code is linked into a DLL.
- Link the exported diagram from Markdown only after the referenced file exists and its content has been reviewed.
- Report corrections in the response when the user asked for review only.

### 7. Verify technical accuracy and publication quality

Reread every claim against current sources:

- interfaces include caller-visible inputs, results, ordering, failures, and relevant performance behavior;
- mutable-state owners and lifetime transitions are explicit;
- tests identify behavioral contracts, while execution artifacts remain private authoring evidence unless separately curated for publication;
- terminology agrees with domain context, public headers, and existing stable decisions;
- limitations describe reader-visible behavior, not internal work queues or editorial TODOs;
- headings and prose read as finished technical documentation rather than a session report.

Run:

```bash
python3 .agents/skills/document-subsystem/scripts/validate_wiki.py \
  --repo-root . \
  --target wiki/<subsystem> \
  --changed-path <each Markdown file changed by this invocation>
```

The validator accepts only paths registered in `wiki/<subsystem>/.wiki-documents`. With one or more `--changed-path` arguments, validate those registered documents. Omit `--changed-path` to validate every registered document; unregistered personal notes remain out of scope and registered documents may not link to them.

Before publication, validate the recursive whitelist union as one release boundary:

```bash
python3 .agents/skills/document-subsystem/scripts/validate_wiki.py \
  --repo-root . \
  --all-public
```

Fix in-scope errors. Report source drift or diagram corrections without expanding scope.

Use `Reviewed` only when the selected document is technically grounded, publication-polished, and passes validation. Otherwise keep `Draft` and state the concrete publication blocker.

## Completion

Complete the invocation when:

- one selected area answers one coherent reader question;
- the wiki prose contains no tracker, portfolio, learning, or session narration;
- claims are grounded in current code, tests, and available evidence;
- requested diagrams have been reviewed without unauthorized edits;
- validation passes without errors;
- every registered document is free of internal measurement values and evaluation rubrics;
- unrelated files remain untouched.

Finish with changed Markdown files, evidence checked, separately reported source drift, diagram review status when applicable, and validation results. Suggest at most one next wiki area.

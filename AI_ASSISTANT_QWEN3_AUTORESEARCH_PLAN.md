# TerminalC Optional Qwen AI Assistant and Autoresearch Plan

**Status:** Internal planning document — not a release feature yet  
**Repository:** TerminalC 1.0.0-beta line  
**Target training hardware:** NVIDIA RTX 5080, assumed 16 GB VRAM; verify with nvidia-smi  
**Scope:** Optional local AI help, product-aware retrieval, and controlled local fine-tuning  
**Public release impact:** None until the feature is implemented, tested, reviewed, and deliberately scheduled for a Saturday release

## 1. Decision summary

We should add an optional local AI assistant, but it must remain a separate assistant layer around TerminalC. The calculator engine, parser, arbitrary-precision arithmetic, FFT/IFFT, plots, scripts, and error handling remain deterministic and authoritative.

The model may:

- explain a result or error in plain language;
- turn a natural-language request into a proposed TerminalC expression;
- find the relevant function, syntax, precision, unit, matrix, FFT, or graph documentation;
- call a small, explicitly allow-listed TerminalC tool and explain its verified result;
- help users discover commands without changing normal calculator behavior.

The model must not:

- perform arithmetic that TerminalC could perform itself;
- replace or reinterpret the parser’s result;
- execute arbitrary shell commands, scripts, file writes, or network requests;
- load at startup or consume resources when AI is disabled;
- silently send prompts, expressions, files, or results to a cloud service;
- be required for any existing TerminalC feature.

The first implementation should use retrieval-augmented generation (RAG) over versioned TerminalC documentation and deterministic tool calls. Fine-tuning is a second stage for behavior, routing, and product-specific response style—not for teaching the model mathematical truth.

## 2. Recommended model and runtime strategy

### Initial model tiers

| Tier | Model | Use | Policy |
|---|---|---|---|
| Tiny/default | Qwen3 0.6B instruct, quantized | Help lookup, intent routing, short explanations | Suitable for the smallest supported machines |
| Recommended training target | Qwen3 1.7B instruct, QLoRA/LoRA adapter | Better product-aware assistance while remaining practical on a 16 GB GPU | Start here unless baseline quality is inadequate |
| Optional quality tier | Qwen3 4B instruct, quantized or QLoRA adapter | More capable explanations and multi-step guidance | Opt-in; benchmark memory and latency before accepting |

The official Qwen3 project lists small dense model sizes including 0.6B, 1.7B, and 4B, documents local llama.cpp usage, and documents MLX support for Apple Silicon. We will use the smallest model that passes the product evaluation gates, rather than assuming a larger model is automatically better: [official Qwen3 repository](https://github.com/QwenLM/Qwen3).

### Runtime

The portable default should be a separate llama.cpp-compatible GGUF helper process. It works across Linux, Windows, and macOS and keeps the C calculator free of Python, CUDA, and model-library dependencies. Qwen documents both the CLI and local OpenAI-compatible llama-server path: [Qwen llama.cpp guide](https://github.com/QwenLM/Qwen3/blob/main/docs/source/run_locally/llama.cpp.md).

Possible providers:

~~~
none       disabled; default and lowest-resource mode
llama      local GGUF helper process; portable default
ollama     optional convenience provider; never a hard dependency
mlx        optional Apple-Silicon provider; not relevant to RTX training
~~~

The base TerminalC installer must not bundle model weights. The user chooses a model path or provider, and the model is loaded lazily only after ai on or an explicit assistant command.

### Proposed user-facing commands

These are design targets, not implementation commitments:

~~~
ai status
ai on
ai off
ai provider llama
ai model /path/to/qwen3-1.7b-q4.gguf
ai ask "How do I calculate a 30-point FFT?"
ai explain
~~~

Ordinary input such as sqrt(2), fft([1,2,3,4]), or precision 50 must continue through the normal parser. AI should only activate through an explicit command or a clearly documented assistant mode.

## 3. Product architecture

~~~
TerminalC process
├── deterministic calculator core (always active)
│   ├── parser and exact rational arithmetic
│   ├── MPFR precision and display rules
│   ├── tensors, matrices, FFT/IFFT, plots, scripts, units
│   └── existing resource limits and error paths
└── optional assistant bridge (off by default)
    ├── local IPC to llama.cpp/Ollama/MLX helper
    ├── versioned documentation retriever
    ├── strict structured-output parser
    ├── allow-listed TerminalC tools
    ├── timeout, token, memory, and output limits
    └── privacy and audit controls
~~~

The assistant bridge should be a child process or local IPC service, not a large library linked into terminalc. If it crashes, times out, exceeds a limit, or returns invalid structured output, TerminalC reports the assistant error and remains usable.

### Allow-listed tools

The first tool surface should be deliberately small:

~~~
help(topic)
list_functions(category?)
get_documentation(topic)
calculate(expression, precision?, angle?, format?)
explain_verified_result(expression, result, precision, angle)
~~~

calculate must call the real TerminalC executable or an internal public API. The model supplies a proposed expression; TerminalC validates and evaluates it. The model never returns a numeric result as authoritative. No generic shell, exec, write_file, or unrestricted plugin tool is allowed.

### Structured response contract

The model should return a schema-validated object, for example:

~~~json
{
  "kind": "tool_call",
  "tool": "calculate",
  "arguments": {
    "expression": "real(ifft(fft([1,2,3,4])))",
    "precision": 20,
    "angle": "radians"
  },
  "explanation": "I will verify the round trip using TerminalC."
}
~~~

The bridge validates the schema, applies resource limits, runs the real calculator, and presents the verified output. Malformed, ambiguous, or unsafe requests should produce a clarification or refusal rather than a best-effort execution.

## 4. Product knowledge plan

### Source-of-truth documents

The initial retrieval index should be generated from the exact checked-out TerminalC version:

~~~
README.md
docs/USAGE.md
docs/FUNCTIONS.md
tests/README.md
tests/eval_catalog.tsv
examples/*.tc
packaging/*/README.md
selected validation summaries and version metadata
~~~

The assistant must identify the TerminalC version and knowledge snapshot used for each answer. Documentation changes invalidate the old index and trigger a new evaluation run.

Retrieved context should include:

- function signature and domain rules;
- precision, truncation/display, and angle-mode semantics;
- tensor shape and complex FFT/IFFT representation;
- graph and plot_save syntax;
- REPL history, arrow keys, Ctrl-C behavior, and script commands;
- platform/package instructions;
- known limits and expected error messages.

### RAG before fine-tuning

RAG should be the first product-knowledge mechanism because it is easy to update when functions, precision semantics, packaging, or commands change. It also lets the assistant quote or point to the current documentation instead of memorizing stale behavior.

### What fine-tuning should teach

Fine-tuning should improve:

- intent classification;
- correct selection of a documented function or tool;
- concise TerminalC-style examples;
- consistent explanations and error recovery;
- correct use of the structured tool-call format;
- when to say “I do not have enough information.”

Fine-tuning should not be used to encode the complete function catalog or to make the model the source of arithmetic truth. The official Qwen documentation describes supervised fine-tuning and LoRA/QLoRA options; Qwen also lists common fine-tuning frameworks such as Unsloth, Swift, and Llama-Factory in its model documentation: [Qwen FAQ](https://github.com/QwenLM/Qwen/blob/main/FAQ.md), [official Qwen3 repository](https://github.com/QwenLM/Qwen3).

## 5. Dataset design

The dataset must be private during development and versioned separately from public release notes. Do not upload user calculations, private validation reports, private keys, or unredacted local paths.

### Training examples

Create JSONL examples in these groups:

| Group | Content |
|---|---|
| Documentation QA | “How do I use nthroot?”, precision rules, units, plots, packaging |
| Intent routing | Natural language → function/help/calculation/explanation request |
| Tool calls | Valid structured calls for help, documentation retrieval, and calculation |
| Verified calculations | Proposed expression → actual TerminalC output → explanation |
| Error handling | Divide by zero, invalid domains, malformed tensors, singular matrices, invalid FFT shapes |
| Precision | p4, p10, p20, p30, p50, p100 examples, including truncation/display behavior |
| Complex math | FFT/IFFT, DFT/IDFT aliases, real/imag extraction, non-power-of-two lengths |
| REPL behavior | History, arrow keys, ans, Ctrl-C, hi, bye, scripts, help |
| Refusal/clarification | Unknown functions, ambiguous units, unsupported shell requests, missing values |
| Platform help | macOS, Windows, Linux, pip, Homebrew, installers, small-machine mode |

The existing 137-callable × 30-case function matrix and graph matrix are excellent sources for private evaluation generation, but they must not simply be copied into training. Split by function family, intent, input pattern, and precision so that held-out cases remain genuinely unseen. Keep a private, immutable evaluation set generated before fine-tuning.

### Dataset rules

1. Every numeric answer in a training example must be generated and checked by TerminalC or an independently verified reference.
2. Preserve exact values where TerminalC preserves exact integers/rationals.
3. Store precision, angle mode, expression, expected exit code, and expected output separately.
4. Include negative examples and “do not hallucinate” examples.
5. Keep product facts tied to a knowledge_version and TerminalC commit.
6. Deduplicate paraphrases across train and validation sets.
7. Do not train on the final test answers or private validation reports.
8. Review a random sample manually before any training run.

Suggested files in a separate internal training workspace:

~~~
ai-training/
├── README.md
├── data/
│   ├── source-manifest.json
│   ├── train.jsonl
│   ├── validation.jsonl
│   └── test-private.jsonl
├── retrieval/
│   ├── document-manifest.json
│   └── index/                 # generated, not public source
├── experiments/
│   ├── results.tsv
│   └── runs/<run-id>/
└── adapters/                  # private model adapters and hashes
~~~

This training workspace should not be mixed into the lightweight TerminalC build or public source distribution.

## 6. RTX 5080 training baseline

The plan assumes the common 16 GB RTX 5080 configuration. Before installing anything, record the actual hardware and software:

~~~sh
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
nvidia-smi
python3 --version
~~~

Use a separate Linux environment or WSL2 training workspace if possible. Keep the production TerminalC checkout and training checkout separate. Confirm that the installed PyTorch/CUDA stack supports the Blackwell GPU before downloading model weights.

### Conservative first run

1. Run Qwen3 0.6B and 1.7B inference without fine-tuning.
2. Build the RAG index from the current TerminalC checkout.
3. Measure baseline tool-call accuracy, response latency, CPU RAM, and VRAM.
4. Start with Qwen3 1.7B instruct and a 4-bit QLoRA adapter.
5. Use a short context window first, such as 2,048 or 4,096 tokens; increase only when evaluation proves it necessary.
6. Start with a small batch and gradient accumulation; let measured peak VRAM decide the next configuration.
7. Try Qwen3 4B only after the 1.7B baseline is stable and the measured memory headroom is documented.

The exact framework and version pins should be selected after the CUDA/PyTorch compatibility check. Candidate training frameworks are Unsloth, Llama-Factory, Swift, or Axolotl; do not add any of them to the TerminalC runtime dependency set.

### Initial QLoRA search space

These are bounded starting experiments, not guaranteed final settings:

~~~text
base model:       Qwen3-1.7B-Instruct
quantization:     4-bit QLoRA
sequence length:  2048, then 4096 if needed
LoRA rank:        16, 32
LoRA alpha:       32, 64
dropout:          0.0, 0.05
learning rate:    two or three conservative values
epochs:           short fixed budget; avoid memorization
output:           adapter only, with base-model and dataset hashes
~~~

Do not begin with full-parameter training. It increases memory use, makes rollback harder, and is unnecessary for the initial product behavior goals.

## 7. Autoresearch adaptation

Karpathy’s autoresearch is a useful experiment-loop pattern, but it is not a drop-in Qwen fine-tuning pipeline. Its official repository uses a single-GPU training setup, a fixed five-minute training budget, a measured validation metric, dedicated experiment branches, and keep/discard decisions: [Karpathy’s autoresearch repository](https://github.com/karpathy/autoresearch).

We should borrow the discipline, not blindly transplant its train.py/prepare.py assumptions.

### Adapted experiment loop

~~~text
baseline
  ↓
generate or select one bounded change
  ↓
train an adapter for a fixed budget
  ↓
run the private held-out TerminalC assistant evaluation
  ↓
record metrics, VRAM, RAM, time, and failure reason
  ↓
keep only if the composite gate improves without regressions
  ↓
repeat on a dedicated research branch
~~~

The research agent may change only training configuration, adapter settings, retrieval chunking, prompt templates, or dataset selection. It may not change TerminalC arithmetic, parser, resource limits, packaging, release files, or public branches.

### Experiment records

Each run must record:

~~~text
run_id
base_model_hash
adapter_hash or commit
dataset_manifest_hash
knowledge_version
config
training_seconds
evaluation_seconds
peak_vram_mb
peak_ram_mb
tool_call_exact_match
expression_routing_accuracy
product_knowledge_accuracy
arithmetic_verification_rate
unsafe_action_rate
unknown/refusal quality
status: keep | discard | crash
reason
~~~

The first version should use a bounded number of experiments per session, for example 10–20, with a human review checkpoint before expanding the search. “Autonomous” must never mean autonomous publishing, autonomous pushing to main, or autonomous access to user files.

### Composite acceptance score

Do not optimize language-model loss alone. A candidate may be kept only if all hard gates pass and the weighted score improves:

~~~text
hard gates:
  TerminalC full validation suite: pass
  arithmetic answers verified through TerminalC: 100%
  unsafe tool/shell actions: 0
  invalid structured calls: below agreed threshold
  no test-data leakage detected

weighted score:
  35% tool-call exactness
  25% product/documentation correctness
  15% expression-routing accuracy
  10% refusal/clarification quality
  10% latency and resource efficiency
   5% response helpfulness reviewed by a human
~~~

If a candidate improves language quality but increases hallucinated arithmetic, unsafe behavior, VRAM, or latency beyond the limits, discard it.

## 8. Validation gates before integration

The feature cannot move from its feature branch toward release until all of these pass.

### Calculator regression

- Existing TerminalC validation remains green.
- The 4,110-case callable matrix remains green.
- Graph, SVG/CSV, FFT/IFFT, precision, REPL, sanitizer, and stress tests remain green.
- AI disabled produces the same behavior, output, exit codes, startup profile, and package contents as before.

### Assistant correctness

- 100% of arithmetic answers shown to users come from verified TerminalC execution.
- High exact-match rate on held-out tool calls and expression routing.
- Correct precision and angle mode are preserved in every tool call.
- FFT/IFFT complex tensor conventions are explained correctly.
- Invalid domains and malformed shapes produce safe, documented responses.
- The assistant identifies uncertainty instead of inventing a function or option.

### Safety and privacy

- No arbitrary shell, file, or network tool exists in the first release.
- Local-only mode is the default.
- Model path, provider, and network behavior are visible in ai status.
- Prompts/results are not persisted unless the user explicitly enables local history.
- User content is not included in training data without explicit selection and redaction.
- Model/adapters are pinned by version and cryptographic hash.
- Timeouts, maximum output tokens, maximum context, and memory limits are tested.

### Resource targets

Target values should be measured, then finalized from real hardware:

~~~text
AI disabled:             no model load and no persistent helper process
AI enabled, tiny model:  bounded CPU/RAM overhead and low idle usage
first response:          documented target on the RTX 5080
peak VRAM:               documented for every model/quantization tier
failure behavior:        calculator remains available after helper failure
~~~

Do not promise a universal latency number until it has been measured on the RTX 5080 and at least one small CPU-only machine.

## 9. Implementation milestones

### M0 — Hardware and environment record

- Capture GPU, driver, CUDA, Python, RAM, disk, and OS details.
- Create a separate private ai-training checkout/worktree.
- Confirm the existing TerminalC branch remains clean except for intentional local validation outputs.

### M1 — Inference-only proof of concept

- Run Qwen3 0.6B and 1.7B locally.
- Add no TerminalC code yet; validate the helper process and model format.
- Record latency, VRAM, RAM, context length, and failure behavior.

### M2 — Documentation retrieval

- Build a versioned index from TerminalC documentation.
- Implement source/version metadata in retrieved context.
- Evaluate help questions and “find the right function” requests.

### M3 — Deterministic tool bridge

- Add the allow-listed structured tool contract.
- Route calculations to the real TerminalC executable/API.
- Add timeouts, output limits, schema validation, and process cleanup.

### M4 — Private assistant evaluation suite

- Generate train/validation/test manifests without leakage.
- Add precision, FFT/IFFT, graphs, tensors, errors, REPL, and packaging cases.
- Save local results in timestamped private directories; do not commit plaintext reports.

### M5 — QLoRA adapter experiments

- Train the 1.7B adapter on the RTX 5080.
- Compare base+RAG versus adapter+RAG.
- Keep adapters separate from base model weights and TerminalC binaries.

### M6 — Autoresearch-style bounded loop

- Add the experiment ledger and dedicated research branches.
- Restrict agent changes to the approved training/retrieval surface.
- Run a bounded batch of experiments and review results manually.

### M7 — Feature-branch integration

- Integrate only the smallest model/provider path that passes all gates.
- Keep AI off by default and optional at build/package time.
- Run the full TerminalC validation and assistant evaluation on macOS, Windows, and Linux.

### M8 — Release readiness

- Update only the concise public documentation needed by users.
- Keep detailed training/evaluation notes internal.
- Group the feature and bug fixes into the next planned beta version.
- Do not merge to main until the complete feature branch is green and reviewed.

## 10. Security, licensing, and distribution rules

- Do not bundle Qwen weights in pip wheels, Homebrew formulas, OS installers, or the small-machine base package.
- Treat model weights and adapters as separately downloaded, versioned artifacts.
- Record the base model license, adapter provenance, source URL, hash, and quantization format.
- Review the applicable Qwen model license and redistribution terms before shipping any model or adapter.
- Keep private training data, encrypted validation archives, and credentials outside the public repository.
- The AI bridge must fail closed: no model means no AI, not a hidden remote fallback.
- AI code belongs on a feature branch and must not be pushed to main automatically.

## 11. Definition of done

This project is ready for a release discussion only when:

1. terminalc works identically with AI disabled.
2. A fresh user can use TerminalC without installing Python, CUDA, or a model.
3. AI is explicitly enabled and its provider/model are visible.
4. Every calculation is executed and verified by TerminalC.
5. Product answers are grounded in the current documentation snapshot.
6. The private held-out suite passes with no leakage or unsafe-action failures.
7. The 5080 benchmark includes time, VRAM, RAM, and failure recovery.
8. Small-machine behavior is measured and bounded.
9. The feature branch passes the existing exhaustive and sanitizer validation.
10. Public docs and release notes remain concise; detailed research artifacts remain internal.

## 12. Immediate next action when starting

Run the hardware capture first, then perform M1 inference-only tests. Do not begin fine-tuning until the base model, runtime, RAG baseline, and private evaluation split are all recorded.

~~~sh
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv
nvidia-smi
~~~

After those results are available, select the exact Qwen3 model quantization and the compatible PyTorch/QLoRA stack. The first implementation should target Qwen3 1.7B with a local GGUF inference path and a separate QLoRA training path.


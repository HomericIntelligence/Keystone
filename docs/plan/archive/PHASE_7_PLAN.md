# Phase 7: AI Integration Plan

**Status**: 📝 Planning
**Target Start**: 2025-12-10
**Estimated Duration**: 4-5 weeks
**Branch**: TBD (claude/phase-7-ai-integration-*)

## Overview

Phase 7 transforms ProjectKeystone from a traditional hierarchical multi-agent system
into an **AI-powered autonomous development platform**. This phase integrates Large
Language Models (LLMs) to enable natural language goal processing, intelligent code
generation, automated debugging, and adaptive task decomposition.

### Current Status (Post-Phase 6)

**What We Have**:

- ✅ Production-ready HMAS deployment (Kubernetes + monitoring)
- ✅ 4-layer agent hierarchy with work-stealing scheduler
- ✅ Chaos engineering and resilience features
- ✅ Comprehensive testing and quality gates
- ✅ Current agents use hardcoded logic for task decomposition

**What Phase 7 Adds**:

- LLM-powered TaskAgents that generate code from natural language
- AI-driven goal decomposition at L2 (ModuleLead) and L1 (ComponentLead)
- Natural language interface for ChiefArchitect
- Code review and debugging agents using LLMs
- Prompt engineering framework for agent interactions
- AI safety guardrails and output validation

---

## Phase 7 Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   User (Natural Language)               │
│          "Build a REST API server with auth"            │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│      ChiefArchitectAgent (L0) + NLP Goal Parser         │
│  - Parse natural language goals                         │
│  - Extract requirements, constraints, dependencies      │
│  - Generate execution plan using LLM                    │
└────────────────────────┬────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│ComponentLead│  │ComponentLead│  │ComponentLead│
│ + AI Planner│  │ + AI Planner│  │ + AI Planner│
│   (L1)      │  │   (L1)      │  │   (L1)      │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                │
       ▼                ▼                ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│ ModuleLead  │  │ ModuleLead  │  │ ModuleLead  │
│ + AI        │  │ + AI        │  │ + AI        │
│ Decomposer  │  │ Decomposer  │  │ Decomposer  │
│   (L2)      │  │   (L2)      │  │   (L2)      │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                │
       ▼                ▼                ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  TaskAgent  │  │  TaskAgent  │  │  TaskAgent  │
│ + LLM Code  │  │ + LLM Debug │  │ + LLM Review│
│  Generator  │  │  Assistant  │  │  Assistant  │
│   (L3)      │  │   (L3)      │  │   (L3)      │
└─────────────┘  └─────────────┘  └─────────────┘
```

---

## LLM Integration Strategy

### Option A: OpenAI API (Recommended for Phase 7)

**Pros**:

- Production-ready, high quality (GPT-4, GPT-3.5-turbo)
- Extensive code generation capabilities
- Function calling for structured outputs
- Stream support for real-time responses

**Cons**:

- Requires API key and costs money
- Network dependency
- Rate limits

**Models**:

- **GPT-4**: Complex reasoning, architecture decisions (L0, L1)
- **GPT-3.5-turbo**: Fast code generation, debugging (L2, L3)
- **GPT-4-turbo**: Balance of quality and speed

### Option B: Local LLMs (Future Phase)

**Pros**:

- No external dependency
- No API costs
- Privacy and data control

**Cons**:

- Requires GPU resources
- Lower quality than GPT-4
- Deployment complexity

**Models**:

- **CodeLlama**: Specialized for code generation
- **WizardCoder**: Strong coding performance
- **DeepSeek-Coder**: Competitive quality

**Decision**: Start with OpenAI API (Phase 7), add local LLM support in Phase 8 or later.

---

## Phase 7 Sub-Phases

### Phase 7.1: LLM Client Infrastructure (Week 1)

**Goal**: Create reusable LLM client library with retry logic and rate limiting

**Tasks**:

1. **Implement OpenAI API client** (`src/ai/openai_client.cpp`) - 6 hours
   - HTTP client using libcurl or cpp-httplib
   - Support for chat completions API
   - Streaming support for real-time responses
   - Error handling and retries
   - Authentication via API key (from environment variable)

2. **Create prompt template system** (`include/ai/prompt_template.hpp`) - 4 hours
   - Template class with variable substitution
   - Support for system/user/assistant messages
   - Few-shot examples management
   - Prompt validation and sanitization

3. **Implement LLM response parser** (`src/ai/response_parser.cpp`) - 3 hours
   - Parse JSON responses from OpenAI
   - Extract code blocks from markdown
   - Validate output format
   - Handle partial/streaming responses

4. **Add rate limiting and retry logic** - 3 hours
   - Token bucket algorithm for rate limiting
   - Exponential backoff for retries
   - Circuit breaker for API failures
   - Metrics for API usage (calls, tokens, latency)

5. **Unit tests for LLM client** (`tests/unit/test_openai_client.cpp`) - 4 hours
   - Mock API responses
   - Test retry logic
   - Test rate limiting
   - Test error handling

**Deliverables**:

- ✅ OpenAI API client library
- ✅ Prompt template system
- ✅ Response parser with code extraction
- ✅ Rate limiting and retry logic
- ✅ Unit tests (20+ tests)

**Estimated Time**: 20 hours

---

### Phase 7.2: NLP Goal Parser (Week 1-2)

**Goal**: Parse natural language goals into structured execution plans

**Tasks**:

1. **Implement NLPGoalParser** (`src/ai/nlp_goal_parser.cpp`) - 6 hours
   - Use GPT-4 to parse user goals
   - Extract requirements, constraints, dependencies
   - Generate component-level breakdown
   - Output structured JSON plan

2. **Create goal parsing prompts** (`prompts/goal_parsing.txt`) - 3 hours
   - System prompt for goal parser
   - Few-shot examples:
     - "Build a REST API" → {components: [Server, Auth, Database]}
     - "Add logging" → {components: [Logging], modules: [Logger, Formatter]}
   - Output schema definition (JSON)

3. **Integrate with ChiefArchitectAgent** - 4 hours
   - Replace hardcoded goal parsing with LLM
   - Call NLPGoalParser in `receiveMessage()` when goal received
   - Convert parsed plan to component assignments
   - Fallback to rule-based parsing if LLM fails

4. **E2E test: NLP goal to execution** (`tests/e2e/phase_7_nlp_goals.cpp`) - 4 hours
   - Send natural language goal to ChiefArchitect
   - Verify goal parsed correctly
   - Verify components assigned correctly
   - Mock LLM responses for deterministic tests

5. **Add goal validation** - 3 hours
   - Check for ambiguous goals (ask for clarification)
   - Validate constraints (time, resources)
   - Detect conflicting requirements
   - Safety checks (no malicious commands)

**Deliverables**:

- ✅ NLP goal parser using GPT-4
- ✅ Structured plan extraction
- ✅ ChiefArchitect integration
- ✅ E2E tests for natural language goals
- ✅ Goal validation and safety checks

**Estimated Time**: 20 hours

---

### Phase 7.3: AI Code Generation (Week 2-3)

**Goal**: TaskAgents generate code using LLMs instead of bash commands

**Tasks**:

1. **Implement AICodeGenerator** (`src/ai/code_generator.cpp`) - 8 hours
   - Use GPT-3.5-turbo for code generation (speed)
   - Prompt engineering for C++20 code
   - Context injection (existing files, signatures)
   - Multi-step generation (plan → code → tests)

2. **Create code generation prompts** (`prompts/code_generation/`) - 5 hours
   - System prompts for C++20, Python, Mojo
   - Few-shot examples for common patterns
   - Code review prompt (find bugs)
   - Test generation prompt

3. **Integrate with TaskAgent** - 6 hours
   - Create `AITaskAgent` subclass
   - Override `executeTask()` to use LLM
   - Input: task description, context (file contents)
   - Output: generated code, tests, documentation
   - Fallback to bash execution for non-code tasks

4. **Add code validation** - 4 hours
   - Syntax check (compile or parse)
   - Style check (clang-format)
   - Security check (static analysis)
   - Unit test generation and execution

5. **E2E test: Code generation workflow** - 5 hours
   - TaskAgent receives "Implement MessageQueue::push()"
   - LLM generates C++ code
   - Code compiled and tested
   - Result returned to ModuleLead
   - Mock LLM for deterministic testing

**Deliverables**:

- ✅ AI code generator for C++20
- ✅ Prompt templates for code generation
- ✅ AITaskAgent implementation
- ✅ Code validation pipeline
- ✅ E2E tests for code generation

**Estimated Time**: 28 hours

---

### Phase 7.4: AI Task Decomposition (Week 3-4)

**Goal**: ModuleLead and ComponentLead use AI for intelligent task breakdown

**Tasks**:

1. **Implement AITaskDecomposer** (`src/ai/task_decomposer.cpp`) - 6 hours
   - Use GPT-4 for complex decomposition
   - Input: module/component goal, constraints
   - Output: list of subtasks with dependencies
   - Adaptive decomposition based on complexity

2. **Create decomposition prompts** (`prompts/task_decomposition.txt`) - 4 hours
   - System prompt for task breakdown
   - Few-shot examples:
     - "Messaging module" → [push(), pop(), size(), tests]
     - "REST API" → [routing, handlers, middleware, auth]
   - Dependency extraction

3. **Integrate with ModuleLead** - 5 hours
   - Replace hardcoded task list with LLM decomposition
   - Call AITaskDecomposer when planning tasks
   - Handle variable task counts (3-10 tasks)
   - Fallback to heuristic decomposition

4. **Integrate with ComponentLead** - 5 hours
   - Use LLM to decompose component into modules
   - Generate module dependencies
   - Adaptive module count (2-5 modules)

5. **E2E test: AI-driven decomposition** - 4 hours
   - ComponentLead receives "Core component"
   - LLM decomposes into modules
   - ModuleLead decomposes module into tasks
   - TaskAgent generates code
   - Full 4-layer AI workflow

**Deliverables**:

- ✅ AI task decomposer for intelligent breakdown
- ✅ Decomposition prompts with examples
- ✅ ModuleLead AI integration
- ✅ ComponentLead AI integration
- ✅ E2E test for full AI workflow

**Estimated Time**: 24 hours

---

### Phase 7.5: AI Code Review & Debugging (Week 4-5)

**Goal**: Automated code review and debugging using LLMs

**Tasks**:

1. **Implement AICodeReviewer** (`src/ai/code_reviewer.cpp`) - 6 hours
   - Review generated code for bugs
   - Suggest improvements (performance, style)
   - Check for security vulnerabilities
   - Generate review comments

2. **Create code review prompts** (`prompts/code_review.txt`) - 3 hours
   - System prompt for code review
   - Checklist: bugs, performance, security, style
   - Output format: structured feedback

3. **Implement AIDebugger** (`src/ai/debugger.cpp`) - 6 hours
   - Analyze error messages and stack traces
   - Suggest fixes using LLM
   - Generate test cases to reproduce bugs
   - Root cause analysis

4. **Create debugging prompts** (`prompts/debugging.txt`) - 3 hours
   - System prompt for debugging
   - Few-shot examples of error → fix
   - Include context (code, error, stack trace)

5. **Integrate review/debug into workflow** - 5 hours
   - After code generation, run AICodeReviewer
   - If bugs found, run AIDebugger
   - Iterate: generate → review → fix (max 3 iterations)
   - Track iteration count and success rate

6. **E2E test: Review and debug loop** - 4 hours
   - Generate code with intentional bug
   - AICodeReviewer detects bug
   - AIDebugger suggests fix
   - Code regenerated and passes

**Deliverables**:

- ✅ AI code reviewer
- ✅ AI debugger assistant
- ✅ Review/debug prompts
- ✅ Iterative fix workflow
- ✅ E2E tests for review/debug

**Estimated Time**: 27 hours

---

### Phase 7.6: Prompt Engineering & Safety (Week 5)

**Goal**: Production-ready prompt engineering and AI safety

**Tasks**:

1. **Create prompt library** (`prompts/`) - 4 hours
   - Organize prompts by agent type (L0, L1, L2, L3)
   - Version control for prompts (v1, v2, etc.)
   - A/B testing infrastructure
   - Prompt templates with variables

2. **Implement prompt optimization** - 5 hours
   - Track prompt performance (success rate, quality)
   - Iterate on prompts based on failures
   - Few-shot example selection
   - Context length optimization (token limits)

3. **Add AI safety guardrails** - 6 hours
   - Content filtering (no malicious code)
   - Output validation (check for hallucinations)
   - Sandbox execution for generated code
   - Rate limiting to prevent abuse
   - Logging all LLM interactions for audit

4. **Implement fallback strategies** - 4 hours
   - If LLM fails, fall back to rule-based logic
   - Retry with different prompts
   - Escalate to human review if needed
   - Metrics for fallback usage

5. **Cost monitoring and optimization** - 3 hours
   - Track token usage and API costs
   - Optimize prompts to reduce tokens
   - Cache responses for repeated queries
   - Use cheaper models where possible

6. **Documentation** (`docs/AI_INTEGRATION.md`) - 4 hours
   - Prompt engineering guide
   - AI safety best practices
   - Cost optimization tips
   - Troubleshooting guide

**Deliverables**:

- ✅ Prompt library with versioning
- ✅ Prompt optimization framework
- ✅ AI safety guardrails
- ✅ Fallback strategies
- ✅ Cost monitoring
- ✅ Documentation: AI_INTEGRATION.md

**Estimated Time**: 26 hours

---

## Success Criteria

### Must Have ✅

- [ ] ChiefArchitect parses natural language goals using LLM
- [ ] TaskAgents generate C++20 code using LLM
- [ ] ModuleLead decomposes modules into tasks using AI
- [ ] ComponentLead decomposes components into modules using AI
- [ ] Code review and debugging integrated
- [ ] AI safety guardrails prevent malicious outputs
- [ ] All existing tests still pass (329+ tests)
- [ ] New E2E tests for AI workflows (10+ tests)

### Should Have 🎯

- [ ] Prompt templates well-documented and versioned
- [ ] Fallback to rule-based logic when LLM fails
- [ ] Cost monitoring for API usage
- [ ] Iterative code improvement (generate → review → fix)
- [ ] AI-generated code passes compilation and tests >80% of time

### Nice to Have 💡

- [ ] Local LLM support (CodeLlama, WizardCoder)
- [ ] Fine-tuned models for specific domains
- [ ] Multi-turn dialogue for clarification
- [ ] AI-generated documentation and READMEs
- [ ] Auto-detection of programming language

---

## Risk Mitigation

### Risk 1: LLM Hallucinations (Generated code doesn't work)

**Impact**: High
**Likelihood**: High
**Mitigation**:

- Validate generated code (compile, test, static analysis)
- Iterative improvement loop (review → fix)
- Fallback to rule-based logic
- Track success rate metrics

### Risk 2: API Costs

**Impact**: Medium
**Likelihood**: Medium
**Mitigation**:

- Use GPT-3.5-turbo for most tasks (cheaper)
- Cache responses for repeated queries
- Optimize prompts to reduce token usage
- Set monthly budget limits

### Risk 3: API Availability/Rate Limits

**Impact**: High
**Likelihood**: Medium
**Mitigation**:

- Implement retry logic with exponential backoff
- Circuit breaker for API failures
- Fallback to local LLM or rule-based logic
- Queue requests to stay under rate limits

### Risk 4: Security (Malicious code generation)

**Impact**: Critical
**Likelihood**: Low
**Mitigation**:

- Sandbox execution of generated code
- Static analysis for security vulnerabilities
- Content filtering on inputs/outputs
- Audit log of all LLM interactions

### Risk 5: Prompt Engineering Complexity

**Impact**: Medium
**Likelihood**: High
**Mitigation**:

- Start with simple prompts, iterate
- Version control for prompts
- A/B testing for prompt improvements
- Document best practices

---

## Performance Expectations

**LLM Latency**:

- **GPT-4**: 5-15 seconds per request (complex reasoning)
- **GPT-3.5-turbo**: 2-5 seconds per request (fast code gen)
- **Streaming**: 100-500 tokens/sec real-time

**Token Usage** (estimated per request):

- **Goal parsing**: 500-1000 tokens
- **Task decomposition**: 800-1500 tokens
- **Code generation**: 1500-3000 tokens
- **Code review**: 2000-4000 tokens

**Cost Estimation** (OpenAI pricing):

- **GPT-4-turbo**: $10/1M input tokens, $30/1M output tokens
- **GPT-3.5-turbo**: $0.50/1M input tokens, $1.50/1M output tokens
- **Monthly estimate** (100 goals/day): ~$50-200/month

**Throughput**:

- Limited by LLM API rate limits (e.g., 500 requests/min for GPT-4)
- Parallel requests possible (up to rate limit)

---

## Testing Strategy

### Unit Tests

1. **OpenAI Client**: Mock API responses, test retry logic
2. **Prompt Templates**: Variable substitution, validation
3. **Response Parser**: Code extraction, error handling
4. **Code Validator**: Syntax check, security check

### Integration Tests

1. **NLP Goal Parser**: Real LLM calls with sample goals
2. **Code Generator**: Generate simple functions, verify compilation
3. **Task Decomposer**: Decompose sample modules
4. **Code Reviewer**: Review code with bugs, verify detection

### E2E Tests

1. **Full AI Workflow**: Natural language → code generation → review
2. **Multi-layer AI**: ChiefArchitect → ComponentLead → ModuleLead → TaskAgent (all using AI)
3. **Error Handling**: LLM failure → fallback to rules
4. **Iterative Improvement**: Generate → review → fix → pass

### Manual Testing

1. Real-world goals: "Build a web server", "Add authentication"
2. Complex decomposition: Multi-component systems
3. Code quality: Generated code readability, maintainability
4. Safety: Try to generate malicious code, verify blocked

---

## Documentation Plan

### Required Documentation

1. **AI_INTEGRATION.md** - Overview of LLM integration
2. **PROMPT_ENGINEERING.md** - Guide to writing effective prompts
3. **AI_SAFETY.md** - Safety guardrails and best practices
4. **LLM_CLIENT.md** - API client usage and configuration
5. **COST_OPTIMIZATION.md** - Tips for reducing API costs

### Prompt Documentation

- Document all prompts in `prompts/` directory
- Include few-shot examples
- Version control with changelog
- Performance metrics per prompt

### README Updates

- Add "AI Integration" section
- Describe natural language interface
- Link to prompt library
- Cost estimates

---

## Example Workflow

### Input (User)

```
"Build a message queue component with thread-safe push/pop operations"
```

### Step 1: ChiefArchitect (NLP Goal Parser)

**LLM Input**:

```
User goal: Build a message queue component with thread-safe push/pop operations

Parse this into a structured plan with components and modules.
```

**LLM Output**:

```json
{
  "components": [
    {
      "name": "Core",
      "modules": ["MessageQueue", "Tests"]
    }
  ],
  "requirements": ["thread-safe", "push operation", "pop operation"],
  "language": "C++20"
}
```

### Step 2: ComponentLead (Task Decomposition)

**LLM Input**:

```
Component: Core
Module: MessageQueue

Decompose this module into implementation tasks.
```

**LLM Output**:

```json
{
  "tasks": [
    "Implement MessageQueue class with std::queue and std::mutex",
    "Implement push() method with lock_guard",
    "Implement try_pop() method with lock_guard and optional",
    "Write unit tests for thread safety"
  ]
}
```

### Step 3: TaskAgent (Code Generation)

**LLM Input**:

```
Task: Implement MessageQueue class with std::queue and std::mutex
Language: C++20

Generate code.
```

**LLM Output**:

```cpp
#pragma once
#include <queue>
#include <mutex>
#include <optional>

template<typename T>
class MessageQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(value));
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
};
```

### Step 4: AICodeReviewer

**LLM Input**:

```
Review this code for bugs, performance, and style issues:
[code from Step 3]
```

**LLM Output**:

```
✅ Thread-safe: Correct use of mutex and lock_guard
✅ Move semantics: Uses std::move for efficiency
✅ Modern C++: Uses std::optional for nullable return
⚠️ Missing: size() method, empty() method
⚠️ Missing: noexcept specification where appropriate
```

### Step 5: Result Aggregation

ModuleLead aggregates all task results, ComponentLead aggregates module results,
ChiefArchitect returns final output to user.

---

## Next Steps

**Week 1**: LLM client infrastructure + NLP goal parsing
**Week 2**: AI code generation for TaskAgents
**Week 3**: AI task decomposition for ModuleLead/ComponentLead
**Week 4**: Code review and debugging agents
**Week 5**: Prompt engineering, safety, and cost optimization

**After Phase 7**: Move to **Phase 8: Distributed System**

- Real network communication (gRPC)
- Multi-node HMAS deployment
- Distributed work-stealing across nodes
- Consensus algorithms for fault tolerance

---

**Status**: 📝 Planning Complete - Ready for Implementation
**Total Estimated Time**: 145 hours (~4-5 weeks)
**Dependencies**: Phase 6 complete (Production deployment)
**Prerequisites**: OpenAI API key, budget for API usage (~$50-200/month)
**Last Updated**: 2025-11-19

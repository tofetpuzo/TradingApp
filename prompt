You are implementing a "trade exception resolver" using a PUSH / CALLBACK design. TWO
things already exist — extend them, do not recreate them:
  1. A Spring Boot 4 project (Java, Maven).
  2. A Python service that resolves trade exceptions (long-running agent work, 60–180s).
Goal: encapsulate the Python service behind a single Java resolver component that is invoked
WHEN AN ERROR (trade exception) OCCURS. The caller must NOT poll — Python pushes the result
back to a Java callback endpoint when it finishes.

## Architecture
  error occurs
     -> ResolverService.onException(exception)        // the encapsulation; called per error
        -> POST Python /resolve  with the exception + a callback URL + an Idempotency-Key
        -> returns IMMEDIATELY (fire-and-forget); no polling
  ...Python runs the agents in the background (60–180s)...
  Python -> POST Java callback endpoint with the result
     -> Java applies the business filter and acts on actionable results

## Contract each side must support (callback style)
Java -> Python (POST /resolve):
  Headers: "Idempotency-Key: <key>"
  Body: { tradeId, exceptionType, description, amount, callbackUrl }
  Python responds 202 immediately, then works in the background. A duplicate Idempotency-Key
  must NOT start a second job.
Python -> Java (POST <callbackUrl>):
  Body: { key, tradeId, status, result }   // status DONE | FAILED
  ResolutionResult: { tradeId, status, confidence, suggestedActions[] }
      status ∈ {"RESOLVED","ESCALATE","REVIEW"}.
  Python must RETRY callback delivery with backoff if Java is unreachable, and persist the
  result so it is never lost if Java is briefly down.

## Java: HTTP client configuration (REQUIRED — a dedicated @Configuration class)
- Create a @Configuration class (e.g. RestClientConfig) that exposes Spring's RestClient as a
  @Bean. Configure on the bean: base URL from a property (e.g. python.base-url), Accept:
  application/json, and socket timeouts (3s connect / 5s read). Inject this bean by
  constructor everywhere the client is used — do NOT `new` a client per call, and do NOT rely
  on the auto-configured builder (you want the timeouts in one place).
- BOOT 4 GOTCHA: Spring Boot 4 removed the older Boot helper classes
  (ClientHttpRequestFactorySettings / ClientHttpRequestFactoryBuilder). Set the timeouts with
  Spring Framework's org.springframework.http.client.SimpleClientHttpRequestFactory using its
  Duration-based setConnectTimeout / setReadTimeout, and pass that factory to the RestClient
  builder.

## Java: components to build
- ResolverService.onException(TradeException): single entry point invoked on an error. Uses
  the injected RestClient to POST /resolve (callbackUrl + Idempotency-Key) and returns without
  waiting. Wrap the submit call in RETRY (≈3 attempts, short backoff, IO/connection errors
  only) + CIRCUIT BREAKER (window 10, 50% threshold, open ~10s), wired PROGRAMMATICALLY
  (decorate the call supplier), not via annotations/AOP. If submit ultimately fails, fall back
  to ESCALATE/MANUAL_REVIEW so the exception never vanishes.
- Callback controller: POST /api/exceptions/callback receiving { key, tradeId, status, result }.
  Must be IDEMPOTENT (may receive duplicate/retried deliveries) — dedupe by key. On receipt:
  status FAILED -> escalate; else apply the business filter and act only on actionable results.
- Health endpoint: GET /api/exceptions/health -> "up".
- Business filter isActionable(ResolutionResult):
    * "RESOLVED" -> drop.
    * "ESCALATE" -> ALWAYS keep, even at confidence 0.0 (durability guarantee).
    * otherwise (e.g. "REVIEW") -> keep only if confidence >= 0.80.
- key = tradeId + ":" + exceptionType (stable, so retries/duplicates dedupe correctly).
- DTOs as records, all ignoring unknown JSON fields: TradeException(tradeId, exceptionType,
  description, amount); ResolutionResult(tradeId, status, confidence, suggestedActions); and a
  callback envelope (key, tradeId, status, result).

## Java: configuration & properties
- application.properties: spring.threads.virtual.enabled=true; server.port; python.base-url;
  and the callback URL Java advertises (host/port/path) — all configurable, nothing hard-coded.

## Python service extension (callback style)
- Accept callbackUrl + Idempotency-Key on POST /resolve; record the job on RECEIPT (so
  duplicates never start a second job); respond 202.
- Run the agent work in the background; on completion POST { key, tradeId, status, result } to
  callbackUrl. Retry delivery with backoff and persist the result until delivered / dead-lettered.

## Environment requirements
- Java 21+ REQUIRED (virtual threads). Build/run with a JDK 21+; if the default JDK is older,
  point JAVA_HOME at a 21+ install (do not assume a path). Spring Boot 4.x, Maven, runnable jar.
- Enable virtual threads. Submit and callback handling are both short operations — no
  long-held connections, no polling.

## Supporting files (create at the project root)
- run.sh: pins JAVA_HOME to a 21+ JDK (overridable via an env var), builds the jar if missing,
  starts both services in the background with logs, and waits until both /health endpoints respond.
- stop.sh: stops whatever listens on the two service ports.
- sample_request.json: exceptions covering every branch (small auto-resolved PRICE_MISMATCH,
  large PRICE_MISMATCH -> escalate, MISSING_SETTLEMENT -> escalate, unknown type -> low-confidence
  review).
- README.md: explain the push/callback flow, why callback instead of polling, the contract,
  how to run, the resolver-logic table, and production hardening notes (in-memory stores should
  move to Redis/DB; consider a durable queue for at-least-once delivery between the services).

## Acceptance criteria (actually run it)
1. Build with a JDK 21+ and start both services.
2. Trigger exceptions covering every branch: onException returns immediately; results arrive
   later via the callback; only escalations are acted on.
3. A duplicate submit (same key) does not start a second Python job; a duplicate callback
   delivery is deduped (handled once).
4. With Java's callback endpoint briefly unavailable, Python retries and the result is not lost.
5. If submit to Python fails outright, Java falls back to ESCALATE/MANUAL_REVIEW.

## Notes
- Keep all environment-specific values (URLs, ports, JDK location) configurable.
- The callback endpoint replaces polling: nothing on the Java side loops waiting for results.
- If the IDE flags virtual-thread APIs as missing, it is resolving against a pre-21 JDK; set
  the project SDK to 21+. The Maven build with a 21+ JDK is the source of truth.

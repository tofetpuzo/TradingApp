
## Java service prompt 

Harden the trade-exception EXECUTE path in the Java service for exactly-once behaviour. The
execute call triggers a real side effect (a DB action on the Python side) and sits behind
Retry + CircuitBreaker, so a retry or a re-drive must NEVER cause the action to happen twice,
and the resolution flag must never disagree with what actually executed. Do not change business
logic — only add the guarantees below. Keep all env values (URLs, EXECUTE_PATH, pool sizes)
configurable; hard-code nothing.

## Shared contract with Python
- Send the idempotency key on every execute call as header "Idempotency-Key: <id>".
- Call the defined EXECUTE_PATH API with the typed DTO. NEVER send raw SQL. Do NOT open a direct
  Java DB connection to Python's tables.
- The database (owned by Python) is the source of truth for "did this execute?". The
  Java/Hazelcast resolution entry is a projection that can be rebuilt from it.

## 1. Idempotency key
- Use the existing per-execution / per-resolve id (confirmed 1:1 with one intended action).
- It MUST be stable across retries (taken from the request/record, never a fresh UUID/timestamp
  per call) and unique per execution.
- Add it as the "Idempotency-Key" header in callExecuteApi(...), keeping the existing RestClient
  POST to EXECUTE_PATH and the existing Retry + CircuitBreaker wrapping.

## 2. executeResolution(...) must be safe to re-run end-to-end
    a. (optional fast path) if the step is already terminal (EXECUTED / EXECUTION_FAILED),
       short-circuit and return the existing result — do not call execute.
    b. call execute (idempotent on Python/DB side).
    c. updateAiResolutionFlag(...) — see section 3.
    d. on execute failure, set EXECUTION_FAILED (or leave non-terminal) — NEVER mark EXECUTED.
   Because execute is idempotent, if the node dies between (b) and (c), re-running the whole
   method is safe: execute won't repeat the action, it just reaches (c) and finishes.

## 3. Verify updateAiResolutionFlag is doing the right thing (ALL must hold)
   This method writes back into the Hazelcast resolution map: it takes the resolution entry that
   already holds Python's analysis and updates that same entry with the execution outcome.
   1. Correct value from reality: EXECUTED only when the execute response indicates success,
      EXECUTION_FAILED otherwise. Derive from the response; never assume success.
   2. Never marks EXECUTED on a failed/throwing execute (no optimistic pre-set).
   3. Idempotent: set-to-terminal-state (not increment/append); re-running writes the same value.
   4. Correct target: writes against the same caseId/stepId used for execute and the key.
   5. Merge, don't overwrite: updates the SAME Hazelcast resolution entry that already holds
      Python's analysis — adds the execution outcome (flag + result) into that entry WITHOUT
      wiping the existing analysis (replaceAll=false). Only ONE writer updates a given resolution
      entry at a time.
   6. Failure not swallowed: if the flag write fails AFTER a successful execute, surface the error
      so the caller/re-drive retries — leaving execute done but the step non-terminal must be a
      RECOVERABLE state, not a silent success.
   7. Consistent with source of truth: matches the DB record; rebuildable from the DB if they
      ever disagree (Hazelcast is in-memory / a projection).

## 4. RestClient connection pool (Apache HttpClient 5)
   Apply to the existing @Qualifier("resolverRestClient") RestClient bean's builder.
   - Dependency: org.apache.httpcomponents.client5:httpclient5 (no version — Spring Boot BOM).
   - PoolingHttpClientConnectionManager: maxConnTotal (~200), maxConnPerRoute (~50, sized to real
     concurrency to Python), connect+socket timeouts via ConnectionConfig (3s/5s), evict idle
     (~30s) and expired connections.
   - HttpComponentsClientHttpRequestFactory.setConnectionRequestTimeout(~2s) so calls fail fast
     when the pool is saturated instead of hanging.
   - Inject the single bean by constructor; never `new` a client per call.
   - BOOT 4 GOTCHA: Spring Boot 4 removed ClientHttpRequestFactorySettings /
     ClientHttpRequestFactoryBuilder — use HttpComponentsClientHttpRequestFactory as below.
   Reference (verified to build/run on Spring Boot 4 + Java 21+):
     PoolingHttpClientConnectionManager cm = PoolingHttpClientConnectionManagerBuilder.create()
             .setMaxConnTotal(200).setMaxConnPerRoute(50)
             .setDefaultConnectionConfig(ConnectionConfig.custom()
                     .setConnectTimeout(Timeout.ofSeconds(3))
                     .setSocketTimeout(Timeout.ofSeconds(5)).build())
             .build();
     CloseableHttpClient httpClient = HttpClients.custom()
             .setConnectionManager(cm)
             .evictIdleConnections(TimeValue.ofSeconds(30))
             .evictExpiredConnections().build();
     HttpComponentsClientHttpRequestFactory factory =
             new HttpComponentsClientHttpRequestFactory(httpClient);
     factory.setConnectionRequestTimeout(Duration.ofSeconds(2));
     RestClient restClient = RestClient.builder()
             .requestFactory(factory).baseUrl(pythonBaseUrl).build();

## 5. Hazelcast single-writer (cluster-wide)
   Updating a resolution entry is a read-merge-write. Ensure ONE writer per entry: before
   executing, do an atomic claim IMap.putIfAbsent(idempotencyKey, "IN_PROGRESS") and proceed only
   if it returns null. Treat this as an optimisation; the DB constraint on the Python side is the
   real guarantee.

## Acceptance criteria (Java side)
   1. Two executes with the same Idempotency-Key -> action runs once; second returns same response.
   2. execute succeeds but response lost (retry) -> one action; resolution entry ends EXECUTED.
   3. crash between execute and updateAiResolutionFlag, then re-run -> no double action; entry
      ends terminal, and Python's analysis is still present (not overwritten).
   4. two concurrent executes for one key across nodes -> exactly one action (putIfAbsent + DB
      constraint block the duplicate).
   5. execute fails -> entry EXECUTION_FAILED, never EXECUTED.
   6. under a burst, connections are reused and never exceed maxConnTotal/maxConnPerRoute; a
      saturated pool fails fast after connectionRequestTimeout.

## TODO (later — separate change, not this one): pickup re-drive
   Ensure the pickup mechanism (reads a stored Python result from the Hazelcast map and calls
   executeResolution) RE-DRIVES steps left non-terminal after a crash, instead of firing once.
   Options: scheduled scan for non-terminal steps, startup scan for IN_PROGRESS, or a queue with
   redelivery until terminal. Safe because execute is idempotent. Track and implement separately.



## Python service prompt

Harden the trade-exception EXECUTE endpoint in the Python service for exactly-once execution.
Java calls this endpoint (behind Retry + CircuitBreaker) to trigger a real DB side effect, so a
retried or duplicate call must NEVER run the action twice. Python owns the database and is the
source of truth for "did this execute?". Do not change business logic — only add the guarantees
below. Keep env values (DB config, ports) configurable; hard-code nothing.

## Shared contract with Java
- Every execute request carries header "Idempotency-Key: <id>" — a stable, per-execution id that
  is identical across retries of the same operation.
- Java sends a typed request body (no raw SQL). Return a response that clearly signals
  success/failure so Java can set the correct resolution flag.

## 1. Dedupe on receipt
   - Read the "Idempotency-Key" header.
   - If a result already exists for that key with status DONE, return the SAME stored response and
     DO NOT run the action again.

## 2. Execute + record atomically
   - Perform the DB action AND record {key -> DONE, response} inside the SAME database
     transaction, so the side effect and the "executed" fact commit together (never one without
     the other).

## 3. Database constraint (the real guarantee)
   - Add a UNIQUE constraint on the idempotency id (or use INSERT ... ON CONFLICT (<id>) DO
     NOTHING), so a duplicate row is physically impossible even under a concurrent race that slips
     past the in-memory dedupe check.

## 4. Result must be persisted / authoritative
   - The stored result (keyed by the idempotency id) is the source of truth. It must survive a
     restart and be able to answer "was this executed, and what was the outcome?" — Java's
     Hazelcast entry is only a projection of this.

## Acceptance criteria (Python side)
   1. Two POSTs with the same Idempotency-Key -> action runs ONCE (verify via row/side-effect
      count); the second returns the same stored response.
   2. Two concurrent POSTs with the same key -> exactly one row/action; the DB constraint blocks
      the duplicate.
   3. A restart between "action committed" and "response returned" -> the retried call finds the
      stored DONE result and returns it without re-executing.
   4. On execution failure, the response clearly indicates failure and no partial/duplicate side
      effect is left behind.

"Python does not receive it" — the usual culprits (check these)
Since the pool itself works (I proved it), if Python isn't receiving the request it's almost always one of these:

baseUrl + uri mismatch. If python.base-url already includes the full path (e.g. http://host:8000/api/resolver/resolve) and you also call .uri("/resolve"), the paths get mangled. Rule: baseUrl = host only (http://host:8000), .uri(...) = the path. Log the final URL to confirm.

requestFactory(factory) not set on the builder. If that line is missing, the RestClient ignores your pool/config entirely and uses defaults — then your timeouts/pool don't apply (and any factory-level issue silently changes behavior).

Missing .contentType(MediaType.APPLICATION_JSON) on POST. Without it the body may not serialize/send as Python expects.

Wrong host from inside the cluster. localhost/127.0.0.1 points at the Java pod itself, not Python. In a cluster use the Python service name (e.g. http://python-service:8000), not localhost.

.retrieve() never subscribed/called. With RestClient you must end the chain with .retrieve().body(...) (or .toBodilessEntity()); if you build the request but never call .retrieve(), nothing is sent.

My money's on #1 or #4 — log the resolved URL right before the call and you'll see it immediately. Want me to add a one-line request-logging interceptor to the config so you can see exactly what URL/headers/body leave the Java service?

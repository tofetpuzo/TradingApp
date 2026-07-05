## Java service — RestClient connection pool (Apache HttpClient 5)
Configure the RestClient bean used for the execute call with a bounded, observable connection
pool instead of the default factory:
- Dependency: org.apache.httpcomponents.client5:httpclient5 (no version — Spring Boot's BOM
  manages it).
- Build the factory from a PoolingHttpClientConnectionManager:
    * maxConnTotal — ceiling across all hosts (e.g. 200).
    * maxConnPerRoute — ceiling per host/route; size it to your real concurrency to Python
      (e.g. 50).
    * connect timeout + socket(read) timeout via ConnectionConfig (e.g. 3s connect / 5s read).
    * evict idle connections (e.g. after 30s) and evict expired connections, so the pool stays healthy.
- On the HttpComponentsClientHttpRequestFactory, set connectionRequestTimeout (e.g. 2s) — the
  max time to wait for a free connection from the pool before failing fast (prevents threads
  piling up when the pool is saturated).
- Inject this single RestClient bean by constructor; never `new` a client per call.
- BOOT 4 GOTCHA: Spring Boot 4 removed the old Boot factory helpers
  (ClientHttpRequestFactorySettings / ClientHttpRequestFactoryBuilder). Use
  HttpComponentsClientHttpRequestFactory with the Apache pooling manager as above.

Reference implementation (verified to build/run on Spring Boot 4 + Java 21+):

    PoolingHttpClientConnectionManager cm = PoolingHttpClientConnectionManagerBuilder.create()
            .setMaxConnTotal(200)
            .setMaxConnPerRoute(50)
            .setDefaultConnectionConfig(ConnectionConfig.custom()
                    .setConnectTimeout(Timeout.ofSeconds(3))
                    .setSocketTimeout(Timeout.ofSeconds(5))
                    .build())
            .build();
    CloseableHttpClient httpClient = HttpClients.custom()
            .setConnectionManager(cm)
            .evictIdleConnections(TimeValue.ofSeconds(30))
            .evictExpiredConnections()
            .build();
    HttpComponentsClientHttpRequestFactory factory =
            new HttpComponentsClientHttpRequestFactory(httpClient);
    factory.setConnectionRequestTimeout(Duration.ofSeconds(2));

    RestClient restClient = RestClient.builder()
            .requestFactory(factory)
            .baseUrl(pythonBaseUrl)
            .build();

Acceptance: under a burst of concurrent execute calls, connections are reused and never exceed
maxConnTotal / maxConnPerRoute; when the pool is saturated, calls fail fast after
connectionRequestTimeout rather than hanging.

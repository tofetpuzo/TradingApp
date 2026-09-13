# The Undertaker — System Diagrams

Render these in VS Code's Markdown preview (with a Mermaid extension) or on GitHub.

## 0. Full architecture — what connects to what

Component + connectivity view: every service, and the direction/purpose of each
link. Airflow is a *trigger* that calls the core and keeps its own metadata DB;
Elasticsearch is written by indexing and read at three RAG points.

```mermaid
flowchart LR
    MB["Mailbox<br/>IMAP · MS Graph · SES"]

    subgraph TRIG["Triggers — any one drives the core"]
        AF["Airflow<br/>scheduler · webserver · triggerer"]
        API["FastAPI<br/>/inbound · /review"]
        QW["Queue worker<br/>SQS consumer"]
    end
    AFDB[("Airflow metadata DB<br/>SQLite/Postgres<br/>DagRuns · TaskInstances · XCom")]
    AF <-->|orchestration state only| AFDB

    subgraph CORE["undertaker/runner.py — framework-agnostic core"]
        direction LR
        ING[ingest] --> NRM[normalize / OCR] --> CLS[classify - LangGraph] --> EXT[extract] --> RTE[route] --> IDX[index]
    end

    MB -->|fetch raw| ING
    AF -->|calls| ING
    API -->|calls| ING
    QW -->|calls| ING

    S3[("S3<br/>raw .eml + blobs")]
    MG[("MongoDB<br/>clients · emails · documents<br/>audit · pipeline_runs")]
    CL["Claude API<br/>Messages + Batch"]

    ING -->|put blobs| S3
    NRM -->|read blob| S3
    ING <-->|state| MG
    CLS <-->|state| MG
    EXT <-->|state| MG
    RTE <-->|state| MG
    IDX <-->|state| MG

    CLS -->|classify call| CL
    EXT -->|extract call| CL

    subgraph ES["Elasticsearch — search + vector store"]
        ESd["documents (search/audit)"]
        ESe["examples (few-shot vectors)"]
        ESm["security-master (instrument kNN)"]
        RRF["RRF hybrid retriever - BM25 + kNN"]
    end
    IDX -->|write denormalized doc| ESd
    CLS -->|few-shot retrieval| ESe
    EXT -->|instrument to ISIN| ESm
    API -->|ops search| RRF

    subgraph DEST["Departments — dispatchers"]
        AS["asset-setup (queue)"]
        PI["p-and-i (http)"]
        AM["asset-maintenance (queue)"]
        TB["trade-booking (http)"]
        HR["human-review (queue)"]
    end
    RTE -->|deliver payload| AS
    RTE --> PI
    RTE --> AM
    RTE --> TB
    RTE --> HR
```

## 1. End-to-end flow

```mermaid
flowchart TB
    MB["📧 Mailbox<br/>IMAP / MS Graph / SES"] -->|raw email + attachments| ING

    subgraph UND["THE UNDERTAKER — AI-TAKE LAYER"]
        direction TB
        ING["① Ingestion<br/>resolve Client · parse envelope · split parts<br/>→ 1 Email + N Documents"]
        OCR["② Pre-process / OCR (per Document)<br/>pypdf · Tesseract/Textract · Claude vision"]
        BRAIN["③–④ Brain — LangGraph + Claude Opus 4.8 (per Document)<br/>classify_intent → classify_taxonomy → gate → extract → validate"]
        ROUTE["⑤ Routing (deterministic code)<br/>group Documents by Email · taxonomy → routing.yaml"]
        IDX["⑥ Indexing / Search"]

        ING --> OCR --> BRAIN --> ROUTE --> IDX
    end

    S3[("🪣 S3<br/>raw blobs")]
    MONGO[("🍃 MongoDB<br/>clients · emails · documents · audit")]
    ES[("🔎 Elasticsearch<br/>documents · examples · security-master")]

    ING --> S3
    ING --> MONGO
    BRAIN -. few-shot examples .-> ES
    BRAIN -. instrument → ISIN/CUSIP .-> ES
    IDX --> ES

    ROUTE --> ASU["asset-setup (queue)"]
    ROUTE --> PI["p-and-i (http)"]
    ROUTE --> AM["asset-maintenance (queue)"]
    ROUTE --> TB["trade-booking (http · HIGH)"]
    ROUTE --> HR["human-review (low confidence)"]

    AIRFLOW["⑦ Airflow — scheduled Batch DAG<br/>poll → ocr → submit_batch → WAIT sensor → collect → route → index"] -.drives.-> UND
    API["FastAPI /inbound · queue worker<br/>real-time path"] -.drives.-> UND
```

## 2. Entity model

```mermaid
erDiagram
    CLIENT ||--o{ EMAIL : sends
    EMAIL  ||--o{ DOCUMENT : contains
    DOCUMENT ||--|| CLASSIFICATION : "gets"
    DOCUMENT ||--o| EXTRACTION : "yields"
    EXTRACTION ||--o{ TRADE : "(trade-booking)"

    CLIENT {
        string client_id PK
        string name
        list   email_domains
        string default_portfolio
        string sla_tier
    }
    EMAIL {
        string email_id PK
        string client_id FK
        string subject
        string raw_s3_key
        string status
    }
    DOCUMENT {
        string doc_id PK
        string email_id FK
        string kind
        string s3_key
        string text
        string status
    }
    CLASSIFICATION {
        string intent
        string document_type
        float  confidence
        bool   is_bulk
    }
    EXTRACTION {
        string extraction_id PK
        json   payload
    }
    TRADE {
        string trade_id PK
        string side
        string instrument
        string identifier
        float  quantity
        float  price
    }
```

## 3. The LangGraph brain (per document)

```mermaid
flowchart TB
    START([document, normalized]) --> CI[classify_intent]
    EX[(RAG: similar<br/>labeled examples)] -. few-shot .-> CI
    CI --> CT[classify_taxonomy]
    CT --> GATE{confidence ≥<br/>threshold?}
    GATE -- no / unclassified --> HR[human_review<br/>park in queue]
    GATE -- yes --> EXT[extract<br/>per-dept extractor]
    EXT -->|trade-booking| LOOP[loop N trades → Trade array]
    LOOP --> RES[(RAG: instrument →<br/>ISIN/CUSIP)]
    RES --> VAL
    EXT --> VAL{validate}
    VAL -- invalid --> HR
    VAL -- ok --> RT[route → dispatcher]
    RT --> IDXX[index to ES] --> DONE([done])
    HR --> DONE
```

## 4. Two run modes

```mermaid
flowchart LR
    subgraph B["Batch (DEFAULT · 500-email scale)"]
        direction TB
        A1[Airflow hourly] --> A2[ingest → ocr] --> A3["ONE Claude Batch<br/>~50% cheaper, async"] --> A4[wait-for-batch sensor] --> A5[collect → extract → route → index]
    end
    subgraph R["Real-time (urgent single email)"]
        direction TB
        R1[FastAPI /inbound or queue] --> R2[land → ocr] --> R3["run_pipeline(doc)<br/>concurrency-capped"] --> R4[route → index]
    end
    B -. same runner.py functions .- R
```

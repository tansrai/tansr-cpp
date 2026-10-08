// 由 tools/generate_api.py 从冻结 schema 原字节生成，请勿手工修改。
static constexpr std::string_view schema_chunks_0[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "archive-sync-v1 additive contract",
  "description": "独立 archive-sync-v1 HTTP 面（doc/rfc/SDK2-档案同步与保留扩展.md；实现 packages/server/src/sdk2/archive-sync.ts）。单入口 POST /v3/sdk2/archive-sync/:id（/api/archive/sync/:id），请求与响应共用信封 { format, identity, method, value | error }。运行时另核：请求体 ≤ 绑定 maxRequestBytes（≤ 94371840）+ 65536；响应体 ≤ 绑定 maxResponseBytes（≤ 1572864），信封整体 ≤ 1638400 bytes；JSON 为规范控制编码（键 ASCII 升序、无空白、整数 ≥ 0、字符串 UTF-8 可往返），解码后重编码须逐字节相等。HTTP 状态：200 成功；403 context_changed / identity_mismatch；429 capacity_exceeded；400 invalid_input；其余错误码 409。本族不在统一错误矩阵四族内，经 /api 门面时错误信封原样透传。",
  "oneOf": [
    {
      "$ref": "#/definitions/Request"
    },
    {
      "$ref": "#/definitions/Response"
    },
    {
      "$ref": "#/definitions/ErrorResponse"
    }
  ],
  "definitions": {
    "Format": {
      "const": "archive-sync-v1"
    },
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-])TANSR_SCHEMA",
    R"TANSR_SCHEMA(*$"
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,18})$",
      "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
    },
    "Scope": {
      "type": "object",
      "additionalProperties": false,
      "required": ["applicationScopeId", "endUserId"],
      "properties": {
        "applicationScopeId": { "$ref": "#/definitions/Id" },
        "endUserId": { "$ref": "#/definitions/Id" }
      },
      "description": "身份两键恒由服务端 authenticate 派生后与绑定固定身份比对；authorizationRevision 不进信封。"
    },
    "Generations": {
      "type": "object",
      "additionalProperties": false,
      "required": ["historyEpoch", "deletionGeneration", "projectionRevision"],
      "properties": {
        "historyEpoch": { "$ref": "#/definitions/Sequence" },
        "deletionGeneration": { "$ref": "#/definitions/Sequence" },
        "projectionRevision": { "$ref": "#/definitions/Sequence" }
      }
    },
    "Target": {
      "type": "object",
      "additionalProperties": false,
      "required": )TANSR_SCHEMA",
    R"TANSR_SCHEMA(["sessionId", "generations"],
      "properties": {
        "sessionId": { "type": "string", "minLength": 1, "maxLength": 512 },
        "generations": { "$ref": "#/definitions/Generations" }
      }
    },
    "Identity": {
      "type": "object",
      "additionalProperties": false,
      "required": ["scope", "bindingId"],
      "properties": {
        "scope": { "$ref": "#/definitions/Scope" },
        "bindingId": { "$ref": "#/definitions/Id" },
        "target": { "$ref": "#/definitions/Target" },
        "sourceId": { "$ref": "#/definitions/Id" },
        "sourceGeneration": { "$ref": "#/definitions/Id" }
      },
      "description": "接收器固定身份（api-client Sdk2ReceiverIdentity 五键；服务端绑定身份至少含 scope / bindingId）。请求 identity 的规范编码须与路径 :id 解析出的绑定身份逐字节相等（identity_mismatch）；只读共享许可的 owner 须含全部五键。"
    },
    "Method": {
      "type": "string",
      "enum": [
        "receive",
        "confirm",
        "pending",
        "head",
        "coverage",
        "identity",
        "operation",
        "records",
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "body-chunk",
        "sync-page",
        "retention",
        "retention-page",
        "apply-retention",
        "reconcile"
      ],
      "description": "方法词表 = archive-sync.ts `methods`（generate-manifest.mjs --check 核两者同集）。value 形随方法由 Sdk2SyncArchiveStore 各方法的入参 / 出参决定，不在本 schema 内逐一定形。"
    },
    "ReadMethod": {
      "type": "string",
      "enum": [
        "head",
        "coverage",
        "identity",
        "records",
        "body-chunk",
        "sync-page",
        "retention",
        "retention-page"
      ],
      "description": "只读共享许可（resolveReadOnly）下允许的方法子集 = archive-sync.ts `readMethods`；其余方法在共享许可下答 403 context_changed。"
    },
    "ErrorCode": {
      "type": "string",
      "enum": [
        "invalid_input",
        "identity_mismatch",
        "integrity_mismatch",
        "capacity_exceeded",
        "pending_ack",
        "receipt_mismatch",
        "context_changed",
        "reentrant",
        "closed",
        "storage_error",
        "reconciliation_required"
      ])TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
      "description": "错误码词表 = archive-sync.ts `codes`；dispatch 抛出的 code 不在词表内一律归 storage_error。"
    },
    "Request": {
      "type": "object",
      "additionalProperties": false,
      "required": ["format", "identity", "method", "value"],
      "properties": {
        "format": { "$ref": "#/definitions/Format" },
        "identity": { "$ref": "#/definitions/Identity" },
        "method": { "$ref": "#/definitions/Method" },
        "value": {
          "description": "方法入参（规范控制编码；receive 可达 2097152 + 4·ceil(maxBatchBytes / 3) bytes，其余 ≤ 1572864 bytes）。"
        }
      },
      "description": "POST 体；content-type 须为 application/json。"
    },
    "Response": {
      "type": "object",
      "additionalProperties": false,
      "required": ["format", "identity", "method", "value"],
      "properties": {
        "format": { "$ref": "#/definitions/Format" },
        "identity": { "$ref": "#/definitions/Identity" },
        "method": { "$ref": "#/definitions/Method" },
        "value": {
          "description)TANSR_SCHEMA",
    R"TANSR_SCHEMA(": "方法出参，≤ 绑定 maxResponseBytes。"
        }
      },
      "description": "200 响应；identity / method 回显请求值，客户端须核对后再消费 value。"
    },
    "ErrorResponse": {
      "type": "object",
      "additionalProperties": false,
      "required": ["format", "identity", "method", "error"],
      "properties": {
        "format": { "$ref": "#/definitions/Format" },
        "identity": {
          "anyOf": [
            { "$ref": "#/definitions/Identity" },
            { "type": "null" }
          ],
          "description": "绑定解析前失败（准入 / 鉴权 / 路径）时为 null。"
        },
        "method": {
          "anyOf": [
            { "$ref": "#/definitions/Method" },
            { "const": "unknown" }
          ],
          "description": "方法解析前失败时为 unknown。"
        },
        "error": {
          "type": "object",
          "additionalProperties": false,
          "required": ["code"],
          "properties": {
            "code": { "$ref": "#/definitions/ErrorCode" }
          }
        }
      },
      "description": "≥ 400 响应；无 messa)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ge / requestId / retryAction 位（平铺四族信封之外的独立形）。"
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_1[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/schemas/sdk2-archive-recovery-v1.schema.json",
  "title": "SDK2 authenticated archive ACK recovery v1",
  "description": "Independent additive recovery contract. The caller durably fixes request before transmission. A completed receipt proves the same archive bytes accepted under the original binding idle lease; it does not rerun a model or rewrite the frozen ACK DTO.",
  "oneOf": [
    {
      "$ref": "#/definitions/AckRebaseRequest"
    },
    {
      "$ref": "#/definitions/AckRebaseReceipt"
    }
  ],
  "definitions": {
    "ArchiveAckRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "$ref": "#/definitions/Generations"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Id"
        },
        "coverage": {
          "$ref": "#/definitions/Coverage"
        },
        "attachments": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveArtifactReceipt"
          },
          "minItems": 0,
          "maxItems": 128,
          "uniqueItems": true
        },
        "ackFormat": {
          "$ref": "#/definitions/ArchiveAckFormat"
        },
        "payloads": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveArtifactReceipt"
          },
          "minItems": 1,
          "maxItems": 128,
          "uniqueItems": true
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "generations",
        "sourceId",
        "sourceGeneration",
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "coverage",
        "attachments",
        "ackFormat",
        "payloads"
      ],
      "additionalProperties": false,
      "$comment": "split-receipts-v1 only: payloads and extra attachments are separate exact first-occurrence receipt sets over the entire closed coverage (including overlap), at most 128 whole contiguous records and at most 128 receipts per category. Same artifact in both categories requires matching witnesses but physical accounting occurs once. Check raw request/control UTF-8 byte limit plus canonical envelope before side effects. Source atomically commits record metadata, body, attachments, contiguous head and original ACK outbox intent before claiming durably-stored. Current authorization then original same-key semantic receipt precedes new epoch/CAS/generation checks. New ACK atomically commits fact, watermark, revision and receipt, never partial records. ACK alone does not release dependencies, old SessionStore, or authorize core execution."
    },
    "RequestIdentity": {
      "t)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ype": "object",
      "properties": {
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "operationEpoch": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "requestId",
        "operationEpoch"
      ],
      "additionalProperties": false,
      "$comment": "Epoch is issued and retired by the trusted host. Same scope/resource/operation/epoch/requestId and semantic content returns its original receipt, different content conflicts. Retired epochs never become fresh; unknown or expired results require query-status, not a new operation. Semantic digest uses SHA256(UTF8(tansr.sdk2.operation.v1)+NUL+canonical({scope:[trustedApp,trustedUser],operation,semantic})), semantic is immutable request body excluding request identity and transport/auth envelope. expectedRevision belongs to semantic content. Fresh auth/epoch validation gates NEW admission; existing-operation status remains queryable under current permission after epoch retirement."
    },
    "I)TANSR_SCHEMA",
    R"TANSR_SCHEMA(d": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "Generations": {
      "type": "object",
      "properties": {
        "historyEpoch": {
          "$ref": "#/definitions/LegacyId"
        },
        "deletionGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "projectionRevision": {
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA("$ref": "#/definitions/Sequence"
        }
      },
      "required": [
        "historyEpoch",
        "deletionGeneration",
        "projectionRevision"
      ],
      "additionalProperties": false
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Existing opaque ID; no trimming, case folding or Unicode normalization. Do not mint authority from this string."
    },
    "Coverage": {
      "type": "object",
      "properties": {
        "fromSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "throughSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "headDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "fromSequence",
        "throughSequence",
        "headDigest"
      ],
      "additionalProperties": false,
      "$comment": "Closed inclusive interval, from<=through. Head is a host-known contiguous chain commitment for this source )TANSR_SCHEMA",
    R"TANSR_SCHEMA(and generation, not a client assertion. SSE cursor is never archive coverage."
    },
    "RecordSequence": {
      "allOf": [
        {
          "$ref": "#/definitions/Sequence"
        },
        {
          "not": {
            "const": "0"
          }
        }
      ],
      "$comment": "Record ordinals start at 1. Zero is only an empty published/acknowledged watermark, never a record ordinal."
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "ArchiveArtifactReceipt": {
      "type": "object",
      "properties": {
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        },
        "state": {
          "const": "durably-stored"
        }
      },
      "required": [
        "artifactId",
        "sha256",
        "state"
      ],
      "additionalProperties": false,
      "$comment": "Raw artifact SHA, never payloadDigest/recordDigest. Exact per-category first-occurrence sets ar)TANSR_SCHEMA",
    R"TANSR_SCHEMA(e derived from every whole record in the requested coverage. No duplicate IDs, foreign objects or divergent witnesses."
    },
    "ArchiveAckFormat": {
      "type": "string",
      "const": "split-receipts-v1"
    },
    "MutationReceipt": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "operation": {
          "type": "string",
          "enum": [
            "binding-create",
            "archive-ack",
            "material-response",
            "binding-close"
          ]
        },
        "semanticDigest": {
          "$ref": "#/definitions/Digest"
        },
        "state": {
          "type": "string",
          "enum": [
            "accepted",
            "completed",
            "rejected"
          ]
        },
        "revision": {
          "$ref": "#/definitions/Sequen)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ce"
        },
        "outcomeRef": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "operation",
        "semanticDigest",
        "state",
        "revision",
        "outcomeRef"
      ],
      "additionalProperties": false,
      "$comment": "Persist admission+semantic witness before side effects. Recover uncertain admissions from durable receipt, never create another operation. accepted does not imply source durable or core consumed. Same request replay returns this same operation identity after authorization recheck. binding-close completed means its durable close intent was committed, not that all resource drain has finished. Final ACK also completes its operation independently of binding resource state. Terminal operation receipts are retained until both their terminal TTL and admission epoch expire; unresolved actual work has separate durable state and cannot be GCed merely because the close receipt expi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(res. Current authorization precedes replay; retained same-key/same-semantics receipt lookup precedes the fresh epoch admission gate. Epoch retirement rejects only new admissions, not truthful recovery of already-admitted same-operation results.",
      "allOf": [
        {
          "if": {
            "properties": {
              "operation": {
                "const": "binding-close"
              }
            }
          },
          "then": {
            "properties": {
              "state": {
                "enum": [
                  "completed",
                  "rejected"
                ]
              }
            }
          }
        }
      ]
    },
    "AckRebaseRequest": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "protocol",
        "bindingId",
        "previous",
        "request"
      ],
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
        "previous": {
          "$ref": "#/definitions/ArchiveAckRequest"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        }
      }
    },
    "AckRebaseReceipt": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "protocol",
        "bindingId",
        "previous",
        "request",
        "next",
        "receipt"
      ],
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "previous": {
          "$ref": "#/definitions/ArchiveAckRequest"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "next": {
          "$ref": "#/definitions/ArchiveAckRequest"
        },
        "receipt": {
          "$ref": "#/definitions/MutationReceipt"
        }
      }
    }
  },
  "x-wire-limits": {
    "ackBytes": 262144,
    "requestEnvelopeBytes": 1024,
    "responseEnvelop)TANSR_SCHEMA",
    R"TANSR_SCHEMA(eBytes": 4096,
    "requestBytes": 263168,
    "responseBytes": 528384
  },
  "$comment": "POST /v3/sdk2/bindings/{bindingId}/archive/ack-rebases. Canonical UTF-8 control JSON, sorted keys and no whitespace. previous is the immutable durable pending ACK; caller must persist request before transmission. This v1 requires a different requestId in the same still-active operationEpoch. Source validates original unaccepted ACK, exact same bytes, required replica, owner/scope/generations, then accepts next and its previous mapping atomically with the original binding idle lease. Only request and expectedRevision may differ. On lost response repeat the exact request; never generate a replacement key or rerun the model. If previous was already completed, rebase conflicts: query the original archive-ack operation and confirm its original receipt. Receiver recovery format is explicit with durable old-to-next ledger and atomic migration of the one current operation/checkpoint, never duplicate coverage or fabricated old r)TANSR_SCHEMA",
    R"TANSR_SCHEMA(eceipt. Old ACK DTO and old receiver factories are unchanged."
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_2[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "sdk2-cache-core-v1 additive contract",
  "description": "旧cache-v1不变。跨字段检查：有效source/binding revision非0；coverage正序且三字段同null；签名时窗≤300000ms；原header≤8192bytes、decoded≤6144bytes；诊断row≤4096bytes。",
  "oneOf": [
    {
      "$ref": "#/definitions/Proof"
    },
    {
      "$ref": "#/definitions/Capabilities"
    },
    {
      "$ref": "#/definitions/Diagnostics"
    },
    {
      "$ref": "#/definitions/Lookup"
    }
  ],
  "definitions": {
    "Lookup": {
      "type": "object",
      "additionalProperties": false,
      "required": ["protocol", "state", "result"],
      "description": "独立lookup入口，完整UTF-8响应≤1048576bytes；非completed的result必须null；任何状态不授权首次执行或清pending。",
      "properties": {
        "protocol": { "const": "sdk2-cache-core-v1" },
        "state": { "enum": ["not-found", "accepted", "result-unknown", "completed", "receipt-expired"] },
        "result": {
          "anyOf": [
            { "type": "null" },
            {
              )TANSR_SCHEMA",
    R"TANSR_SCHEMA("type": "object", "additionalProperties": false,
              "required": ["status", "contentType", "body"],
              "properties": {
                "status": { "type": "integer", "minimum": 100, "maximum": 599 },
                "contentType": { "type": "string", "minLength": 1, "maxLength": 128 },
                "body": { "type": "string", "maxLength": 262144, "description": "运行时另核UTF-8≤262144bytes，原响应正文不重编码。" },
                "headers": { "type": "object", "additionalProperties": false, "properties": {
                  "cost": { "type": "string", "maxLength": 1024 },
                  "balance": { "type": "string", "maxLength": 1024 },
                  "priceTable": { "type": "string", "maxLength": 1024 }
                } }
              }
            }
          ]
        }
      },
      "if": { "properties": { "state": { "not": { "const": "completed" } } } },
      "then": { "properties": { "result": { "type": "null" } } }
    },
    "Id": {
      "type": "string",
      "minLength": 1,
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,18})$",
      "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "Coverage": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "fromSequence",
        "throughSequence",
        "headDigest"
      ],
      "properties": {
        "fromSequence": {
          "type": "string",
          "pattern": "^(0|[1-9][0-9]{0,18})$",
          "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
        },
        "throughSequence": {
          "type": "string",
          "pattern": "^(0|[1-9][0-9]{0,18})$",
          "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
        },
        "headDigest": {
          "type": "string",
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(pattern": "^[a-f0-9]{64}$"
        }
      }
    },
    "Material": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "format",
        "source",
        "coverage",
        "assemblerVersion",
        "sourceHeadDigest"
      ],
      "properties": {
        "format": {
          "const": "sdk2-governance-material-v1"
        },
        "source": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "sourceId",
            "sourceGeneration",
            "snapshotDigest",
            "generations"
          ],
          "properties": {
            "sourceId": {
              "type": "string",
              "minLength": 1,
              "maxLength": 128,
              "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
            },
            "sourceGeneration": {
              "anyOf": [
                {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128,
            )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
                },
                {
                  "type": "null"
                }
              ]
            },
            "snapshotDigest": {
              "anyOf": [
                {
                  "type": "string",
                  "pattern": "^[a-f0-9]{64}$"
                },
                {
                  "type": "null"
                }
              ]
            },
            "generations": {
              "type": "object",
              "additionalProperties": false,
              "required": [
                "historyEpoch",
                "deletionGeneration",
                "projectionRevision"
              ],
              "properties": {
                "historyEpoch": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 512
                },
                "deletionGeneration": {
                  "type": "string",
                  "pattern": "^(0|[1-9][0-9]{0,18})$",
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(               "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
                },
                "projectionRevision": {
                  "type": "string",
                  "pattern": "^(0|[1-9][0-9]{0,18})$",
                  "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
                }
              }
            }
          }
        },
        "coverage": {
          "anyOf": [
            {
              "$ref": "#/definitions/Coverage"
            },
            {
              "type": "null"
            }
          ]
        },
        "assemblerVersion": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 128,
              "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
            },
            {
              "type": "null"
            }
          ]
        },
        "sourceHeadDigest": {
          "anyOf": [
            {
              "type": "string",
              "pattern": "^[a-f0-9]{64}$"
            })TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
            {
              "type": "null"
            }
          ]
        }
      }
    },
    "Core": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "assemblerVersion",
        "effectiveInput",
        "materials"
      ],
      "properties": {
        "assemblerVersion": {
          "const": "tansr-core-request-v1"
        },
        "effectiveInput": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "systemSha256",
            "toolsSha256"
          ],
          "properties": {
            "systemSha256": {
              "type": "string",
              "pattern": "^[a-f0-9]{64}$"
            },
            "toolsSha256": {
              "type": "string",
              "pattern": "^[a-f0-9]{64}$"
            }
          }
        },
        "materials": {
          "type": "array",
          "maxItems": 8,
          "items": {
            "$ref": "#/definitions/Material"
          }
        }
      }
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
    "Intent": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "applicationScopeId",
        "endUserId",
        "sourceNamespace",
        "sourceId",
        "sourceRevision",
        "deletionGeneration",
        "history",
        "runtimeSessionId",
        "bindingId",
        "bindingRevision",
        "projectionRef",
        "requestId",
        "payloadSha256",
        "core"
      ],
      "properties": {
        "applicationScopeId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "endUserId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 512
        },
        "sourceNamespace": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "sourceId": {
          "type": "string",
          "minLength": 1,
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA("maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "sourceRevision": {
          "type": "string",
          "pattern": "^(0|[1-9][0-9]{0,18})$",
          "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
        },
        "deletionGeneration": {
          "type": "string",
          "pattern": "^(0|[1-9][0-9]{0,18})$",
          "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
        },
        "history": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "generation",
            "commit"
          ],
          "properties": {
            "generation": {
              "type": "string",
              "pattern": "^(0|[1-9][0-9]{0,18})$",
              "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
            },
            "commit": {
              "type": "string",
              "pattern": "^[a-f0-9]{64}$"
            }
          }
        },
        "runtimeSessionId": {
          "type": "stri)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ng",
          "minLength": 1,
          "maxLength": 512
        },
        "bindingId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "bindingRevision": {
          "type": "string",
          "pattern": "^(0|[1-9][0-9]{0,18})$",
          "description": "十进制序列 < 9223372036854775807，运行时检查上界。"
        },
        "projectionRef": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "requestId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "payloadSha256": {
          "type": "string",
          "pattern": "^[a-f0-9]{64}$"
        },
        "core": {
          "$ref": "#/definitions/Core"
        }
      }
    },
    "Proof": {
      "type": "object",
      "additionalProperties": false,
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "required": [
        "protocol",
        "issuerId",
        "keyId",
        "issuedAtMs",
        "expiresAtMs",
        "intent",
        "signature"
      ],
      "properties": {
        "protocol": {
          "const": "sdk2-cache-core-v1"
        },
        "issuerId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "keyId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "issuedAtMs": {
          "type": "integer",
          "minimum": 0
        },
        "expiresAtMs": {
          "type": "integer",
          "minimum": 1
        },
        "intent": {
          "$ref": "#/definitions/Intent"
        },
        "signature": {
          "type": "string",
          "pattern": "^[a-f0-9]{64}$"
        }
      }
    },
    "Capabilities": {
      "type": "object",
      "additionalProper)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ties": false,
      "required": [
        "protocol",
        "features",
        "limits"
      ],
      "properties": {
        "protocol": {
          "const": "sdk2-cache-core-v1"
        },
        "features": {
          "type": "array",
          "maxItems": 2,
          "items": {
            "enum": [
              "trusted-core-projection-v1",
              "bounded-diagnostics-v1"
            ]
          }
        },
        "limits": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "proofHeaderBytes",
            "materials",
            "diagnosticBytes",
            "responseBytes",
            "diagnosticsPageRows",
            "diagnosticRetentionMs"
          ],
          "properties": {
            "proofHeaderBytes": {
              "const": 8192
            },
            "materials": {
              "const": 8
            },
            "diagnosticBytes": {
              "const": 4096
            },
            "responseBytes": {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             "const": 65536
            },
            "diagnosticsPageRows": {
              "const": 100
            },
            "diagnosticRetentionMs": {
              "const": 86400000
            }
          }
        }
      }
    },
    "Diagnostic": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "diagnosticId",
        "observedAtMs",
        "state",
        "attempts",
        "core",
        "gateway",
        "usage"
      ],
      "properties": {
        "diagnosticId": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
        },
        "observedAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "state": {
          "enum": [
            "accepted",
            "completed",
            "result-unknown"
          ]
        },
        "attempts": {
          "type": "integer",
          "minimum": )TANSR_SCHEMA",
    R"TANSR_SCHEMA(1,
          "maximum": 10000
        },
        "core": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "state",
            "system",
            "tools",
            "materials",
            "materialCount",
            "coverageCount"
          ],
          "properties": {
            "state": {
              "const": "verified"
            },
            "system": {
              "enum": [
                "equal",
                "different",
                "unknown"
              ]
            },
            "tools": {
              "enum": [
                "equal",
                "different",
                "unknown"
              ]
            },
            "materials": {
              "enum": [
                "equal",
                "different",
                "unknown"
              ]
            },
            "materialCount": {
              "type": "integer",
              "minimum": 0,
              "maximum": 8
            },
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "coverageCount": {
              "type": "integer",
              "minimum": 0,
              "maximum": 8
            }
          }
        },
        "gateway": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "relation",
            "commonSegments",
            "totalSegments",
            "firstChangedSegment"
          ],
          "properties": {
            "relation": {
              "enum": [
                "baseline",
                "comparable",
                "not-comparable"
              ]
            },
            "commonSegments": {
              "type": "integer",
              "minimum": 0,
              "maximum": 64
            },
            "totalSegments": {
              "type": "integer",
              "minimum": 0,
              "maximum": 64
            },
            "firstChangedSegment": {
              "anyOf": [
                {
                  "type": "integer",
                  "minimum": 0,
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "maximum": 64
                },
                {
                  "type": "null"
                }
              ]
            }
          }
        },
        "usage": {
          "type": "object",
          "additionalProperties": false,
          "required": [
            "observation",
            "inputTokens",
            "outputTokens",
            "cacheReadTokens",
            "cacheWriteTokens",
            "cacheReadEvidence",
            "cost",
            "currency",
            "savedNet"
          ],
          "properties": {
            "observation": {
              "enum": [
                "unavailable",
                "estimated",
                "reported"
              ]
            },
            "inputTokens": {
              "anyOf": [
                {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                {
                  "type": "null"
                }
              ]
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           },
            "outputTokens": {
              "anyOf": [
                {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                {
                  "type": "null"
                }
              ]
            },
            "cacheReadTokens": {
              "anyOf": [
                {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                {
                  "type": "null"
                }
              ]
            },
            "cacheWriteTokens": {
              "anyOf": [
                {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                {
                  "type": "null"
                }
              ]
            },
            "cacheReadEvidence": {
              "enum": [
                "positi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ve",
                "unknown"
              ]
            },
            "cost": {
              "anyOf": [
                {
                  "type": "string",
                  "maxLength": 128,
                  "pattern": "^-?\\d+(?:\\.\\d+)?$"
                },
                {
                  "type": "null"
                }
              ]
            },
            "currency": {
              "anyOf": [
                {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32
                },
                {
                  "type": "null"
                }
              ]
            },
            "savedNet": {
              "anyOf": [
                {
                  "type": "string",
                  "maxLength": 128,
                  "pattern": "^-?\\d+(?:\\.\\d+)?$"
                },
                {
                  "type": "null"
                }
              ]
            }
          }
        }
      }
    },
    "Diagnosti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(cs": {
      "type": "object",
      "additionalProperties": false,
      "required": [
        "protocol",
        "rows",
        "next"
      ],
      "properties": {
        "protocol": {
          "const": "sdk2-cache-core-v1"
        },
        "rows": {
          "type": "array",
          "maxItems": 100,
          "items": {
            "$ref": "#/definitions/Diagnostic"
          }
        },
        "next": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 150,
              "pattern": "^[0-9]+\\.[A-Za-z0-9][A-Za-z0-9._~-]*$"
            },
            {
              "type": "null"
            }
          ]
        }
      }
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_3[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://schemas.tansr.com/drafts/sdk2-cache-v1.schema.json",
  "title": "SDK2 private logical cache continuity candidate v0.2",
  "$comment": "Unreleased independent SDK2 contract; SDK1/TWP/IR unchanged. Named DTO validation required. Public, gateway-only and host-only definitions have different authority and cannot be interchanged based on root anyOf. TTL/authorization/CAS/durable transaction/byte caps are runtime obligations.",
  "anyOf": [
    {
      "$ref": "#/definitions/CapabilitiesResponse"
    },
    {
      "$ref": "#/definitions/OpenRequest"
    },
    {
      "$ref": "#/definitions/GatewayOpenRequest"
    },
    {
      "$ref": "#/definitions/OpenResponse"
    },
    {
      "$ref": "#/definitions/BindingView"
    },
    {
      "$ref": "#/definitions/BindingQuery"
    },
    {
      "$ref": "#/definitions/RenewRequest"
    },
    {
      "$ref": "#/definitions/CloseRequest"
    },
    {
      "$ref": "#/definitions/RotateRequest")TANSR_SCHEMA",
    R"TANSR_SCHEMA(
    },
    {
      "$ref": "#/definitions/OperationQuery"
    },
    {
      "$ref": "#/definitions/MutationReceipt"
    },
    {
      "$ref": "#/definitions/ProjectionView"
    },
    {
      "$ref": "#/definitions/ProjectionWitness"
    },
    {
      "$ref": "#/definitions/ProjectionPublish"
    },
    {
      "$ref": "#/definitions/ExchangeMetadata"
    },
    {
      "$ref": "#/definitions/MappingRecord"
    },
    {
      "$ref": "#/definitions/Diagnostic"
    },
    {
      "$ref": "#/definitions/DiagnosticQuery"
    },
    {
      "$ref": "#/definitions/DiagnosticPage"
    },
    {
      "$ref": "#/definitions/ErrorResponse"
    },
    {
      "$ref": "#/definitions/RebindRequest"
    },
    {
      "$ref": "#/definitions/PolicyRefresh"
    },
    {
      "$ref": "#/definitions/GatewayLink"
    },
    {
      "$ref": "#/definitions/GatewayAdmissionRecord"
    }
  ],
  "definitions": {
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Z)TANSR_SCHEMA",
    R"TANSR_SCHEMA(a-z0-9._~-]*$"
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Existing opaque ID; no trimming, case folding or Unicode normalization. Do not mint authority from this string."
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "Generations": {
      "type": "object",
      "properties": {
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "historyEpoch": {
          "$ref": "#/definitions/LegacyId"
        },
        "deletionGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "projectionRevision": {
          "$ref": "#/definitions/Sequence"
        }
      },
      "required": [
        "historyEpoch",
        "deletionGeneration",
        "projectionRevision"
      ],
      "additionalProperties": false
    },
    "Coverage": {
      "type": "object",
      "properties": {
        "fromSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "throughSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "headDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "fromSequence",
        "throughSequence",
        "headDigest"
      ],
      "additionalProperties": false,
      "$comment": "Closed inclusive interval, from<=through. Head is a host-known contiguous chain commitment for this source and generation, not a client )TANSR_SCHEMA",
    R"TANSR_SCHEMA(assertion. SSE cursor is never archive coverage."
    },
    "Scope": {
      "type": "object",
      "properties": {
        "applicationScopeId": {
          "$ref": "#/definitions/Id"
        },
        "endUserId": {
          "$ref": "#/definitions/LegacyId"
        },
        "authorizationRevision": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "applicationScopeId",
        "endUserId",
        "authorizationRevision"
      ],
      "additionalProperties": false,
      "$comment": "Host/API-owned only. Public request scope is derived from current authentication, never body/OS/platform labels. Application and original end-user remain a tuple. Core scope uses its real authorization revision; gateway scope uses null because the API does not know it. Identity is always the original application/user tuple; neither revision nor HMAC substitut)TANSR_SCHEMA",
    R"TANSR_SCHEMA(es for current authentication."
    },
    "EpochView": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/Id"
        },
        "issuedAt": {
          "type": "string",
          "format": "date-time"
        },
        "expiresAt": {
          "type": "string",
          "format": "date-time"
        },
        "state": {
          "const": "active"
        }
      },
      "required": [
        "id",
        "issuedAt",
        "expiresAt",
        "state"
      ],
      "additionalProperties": false,
      "$comment": "Trusted host initialization/independent rotator durably prepares current epoch. GET reads snapshot and never mints/renews one. No current live epoch => epoch_unavailable for admission. Rotation disables old write admission, never hides already admitted operation status; retired/unknown IDs cannot be recreated from requests. S1 epoch lifetime defaults to 3600000ms, hard maximum 86400000ms; trusted host validates 0<expiresAt-issuedAt<=effective )TANSR_SCHEMA",
    R"TANSR_SCHEMA(epochLifetimeMs. Terminal receipt cannot be collected before BOTH epoch expiry and terminal retention expiry; unresolved operations remain. An active epoch is never a license to recreate a collected request ID."
    },
    "Protocol": {
      "const": "sdk2-cache-v1"
    },
    "Feature": {
      "const": "private-logical-cache-v1"
    },
    "Audience": {
      "type": "string",
      "enum": [
        "serve-cache",
        "gateway-cache"
      ]
    },
    "Ticket": {
      "type": "string",
      "pattern": "^[A-Za-z0-9_-]{43}$",
      "minLength": 43,
      "maxLength": 43,
      "$comment": "32 random bytes, canonical base64url without padding. Scope/audience-bound reference, not self-asserted authority; never log or use as provider key."
    },
    "RequestIdentity": {
      "type": "object",
      "properties": {
        "operationEpoch": {
          "$ref": "#/definitions/Id"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "operat)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ionEpoch",
        "requestId"
      ],
      "additionalProperties": false
    },
    "KeyedDigest": {
      "type": "object",
      "properties": {
        "algorithm": {
          "const": "hmac-sha256"
        },
        "keyId": {
          "$ref": "#/definitions/Id"
        },
        "digest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "algorithm",
        "keyId",
        "digest"
      ],
      "additionalProperties": false
    },
    "Limits": {
      "type": "object",
      "properties": {
        "controlBytes": {
          "type": "integer",
          "minimum": 4096,
          "maximum": 65536
        },
        "exchangeBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 33554432
        },
        "projectionBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 262144
        },
        "projectionSegments": {
          "type": "integer",
          "minimum": 1,
          "maximum)TANSR_SCHEMA",
    R"TANSR_SCHEMA(": 64
        },
        "ticketTtlMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 604800000
        },
        "mappingIdleMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 604800000
        },
        "mappingLifetimeMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 2592000000
        },
        "bindingsPerLogical": {
          "type": "integer",
          "minimum": 1,
          "maximum": 16
        },
        "mappingsPerUser": {
          "type": "integer",
          "minimum": 1,
          "maximum": 1000
        },
        "mappingsPerApplication": {
          "type": "integer",
          "minimum": 1,
          "maximum": 10000
        },
        "mappingsGlobal": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100000
        },
        "mappingBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 16384
        },
        "receiptsGlo)TANSR_SCHEMA",
    R"TANSR_SCHEMA(bal": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100000
        },
        "receiptBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 32768
        },
        "receiptRetentionMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 604800000
        },
        "epochLifetimeMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 86400000
        },
        "diagnosticRowsPerUser": {
          "type": "integer",
          "minimum": 1,
          "maximum": 1000
        },
        "diagnosticRowsPerApplication": {
          "type": "integer",
          "minimum": 1,
          "maximum": 10000
        },
        "diagnosticRowsGlobal": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100000
        },
        "diagnosticBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 4096
        },
        "diagnosticRetentionMs": {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "type": "integer",
          "minimum": 60000,
          "maximum": 86400000
        },
        "diagnosticSamplePerMillion": {
          "type": "integer",
          "minimum": 0,
          "maximum": 1000000
        },
        "diagnosticsPageRows": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100
        },
        "negotiatedTtlMs": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 60000
        },
        "retainedRowsPerUser": {
          "type": "integer",
          "minimum": 1,
          "maximum": 10000
        },
        "retainedRowsPerApplication": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100000
        },
        "retainedRowsGlobal": {
          "type": "integer",
          "minimum": 1,
          "maximum": 1000000
        },
        "retainedBytesPerUser": {
          "type": "integer",
          "minimum": 1,
          "maximum": 16777216
        },
        "retainedBytesPerApplication": {
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "type": "integer",
          "minimum": 1,
          "maximum": 268435456
        },
        "retainedBytesGlobal": {
          "type": "integer",
          "minimum": 1,
          "maximum": 2147483648
        }
      },
      "required": [
        "controlBytes",
        "exchangeBytes",
        "projectionBytes",
        "projectionSegments",
        "ticketTtlMs",
        "mappingIdleMs",
        "mappingLifetimeMs",
        "bindingsPerLogical",
        "mappingsPerUser",
        "mappingsPerApplication",
        "mappingsGlobal",
        "mappingBytes",
        "receiptsGlobal",
        "receiptBytes",
        "receiptRetentionMs",
        "epochLifetimeMs",
        "diagnosticRowsPerUser",
        "diagnosticRowsPerApplication",
        "diagnosticRowsGlobal",
        "diagnosticBytes",
        "diagnosticRetentionMs",
        "diagnosticSamplePerMillion",
        "diagnosticsPageRows",
        "negotiatedTtlMs",
        "retainedRowsPerUser",
        "retainedRowsPerApplication",
        "retai)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nedRowsGlobal",
        "retainedBytesPerUser",
        "retainedBytesPerApplication",
        "retainedBytesGlobal"
      ],
      "additionalProperties": false
    },
    "CapabilitiesResponse": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "audience": {
          "$ref": "#/definitions/Audience"
        },
        "features": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Feature"
          },
          "minItems": 0,
          "maxItems": 1
        },
        "gateway": {
          "type": "string",
          "enum": [
            "confirmed",
            "unsupported",
            "unavailable",
            "not-applicable"
          ]
        },
        "availability": {
          "const": "legacy-complete"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "limits": {
          "$ref": "#/definitions/Limits"
        },
        "operation)TANSR_SCHEMA",
    R"TANSR_SCHEMA(Epoch": {
          "anyOf": [
            {
              "$ref": "#/definitions/EpochView"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "audience",
        "features",
        "gateway",
        "availability",
        "revision",
        "limits",
        "operationEpoch"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "features": {
                "minItems": 1
              }
            }
          },
          "then": {
            "properties": {
              "gateway": {
                "type": "string",
                "enum": [
                  "confirmed",
                  "not-applicable"
                ]
              },
              "operationEpoch": {
                "$ref": "#/definitions/EpochView"
              }
            }
          }
        },
        {
          "if": {
            "properti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(es": {
              "audience": {
                "const": "serve-cache"
              }
            }
          },
          "then": {
            "properties": {
              "gateway": {
                "type": "string",
                "enum": [
                  "confirmed",
                  "unsupported",
                  "unavailable"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "audience": {
                "const": "gateway-cache"
              }
            }
          },
          "then": {
            "properties": {
              "gateway": {
                "const": "not-applicable"
              }
            }
          }
        }
      ]
    },
    "OpenIntent": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "new"
            }
          },
          "required": [
            "kind"
          ],
          "additio)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "resume"
            },
            "ticket": {
              "$ref": "#/definitions/Ticket"
            }
          },
          "required": [
            "kind",
            "ticket"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "fork"
            },
            "parentTicket": {
              "$ref": "#/definitions/Ticket"
            }
          },
          "required": [
            "kind",
            "parentTicket"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "import"
            }
          },
          "required": [
            "kind"
          ],
          "additionalProperties": false
        }
      ]
    },)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
    "OpenRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "intent": {
          "$ref": "#/definitions/OpenIntent"
        },
        "requiredFeature": {
          "$ref": "#/definitions/Feature"
        }
      },
      "required": [
        "protocol",
        "request",
        "sessionId",
        "intent",
        "requiredFeature"
      ],
      "additionalProperties": false
    },
    "GatewayOpenRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "runtimeSessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "intent": {
          "$ref": "#/definitions/OpenIntent)TANSR_SCHEMA",
    R"TANSR_SCHEMA("
        },
        "requiredFeature": {
          "$ref": "#/definitions/Feature"
        }
      },
      "required": [
        "protocol",
        "request",
        "runtimeSessionId",
        "intent",
        "requiredFeature"
      ],
      "additionalProperties": false
    },
    "ProjectionView": {
      "type": "object",
      "properties": {
        "projectionRef": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "status": {
          "type": "string",
          "enum": [
            "current",
            "rebuild-required",
            "missing"
          ]
        },
        "reason": {
          "type": "string",
          "enum": [
            "unchanged",
            "policy-changed",
            "generation-changed",
            "provider-changed",
            "source-missing",
            "legacy-missing",
            "key-rotated"
          ]
        }
      },
      "required": [
        "projectionRef")TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
        "revision",
        "status",
        "reason"
      ],
      "additionalProperties": false
    },
    "BindingView": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "audience": {
          "$ref": "#/definitions/Audience"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "logicalRef": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "state": {
          "type": "string",
          "enum": [
            "active",
            "closed",
            "revoked",
            "expired"
          ]
        },
        "mode": {
          "const": "private-logical-v1"
        },
        "groupGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "projection": {
          "anyOf": [
            {
              "$ref": "#/definitions/ProjectionView"
            },
            {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "type": "null"
            }
          ]
        },
        "expiresAt": {
          "type": "string",
          "format": "date-time"
        },
        "availability": {
          "const": "legacy-complete"
        }
      },
      "required": [
        "protocol",
        "audience",
        "bindingId",
        "logicalRef",
        "revision",
        "state",
        "mode",
        "groupGeneration",
        "projection",
        "expiresAt",
        "availability"
      ],
      "additionalProperties": false
    },
    "OpenResponse": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "binding": {
          "$ref": "#/definitions/BindingView"
        },
        "ticket": {
          "$ref": "#/definitions/Ticket"
        },
        "ticketExpiresAt": {
          "type": "string",
          "format": "date-time"
        },
        "r)TANSR_SCHEMA",
    R"TANSR_SCHEMA(elation": {
          "type": "string",
          "enum": [
            "new",
            "resumed",
            "forked",
            "imported"
          ]
        }
      },
      "required": [
        "protocol",
        "request",
        "binding",
        "ticket",
        "ticketExpiresAt",
        "relation"
      ],
      "additionalProperties": false
    },
    "BindingQuery": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "protocol",
        "bindingId"
      ],
      "additionalProperties": false
    },
    "BindingMutation": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expected)TANSR_SCHEMA",
    R"TANSR_SCHEMA(Revision": {
          "$ref": "#/definitions/Sequence"
        },
        "ticket": {
          "$ref": "#/definitions/Ticket"
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "ticket"
      ],
      "additionalProperties": false
    },
    "RenewRequest": {
      "$ref": "#/definitions/BindingMutation"
    },
    "CloseRequest": {
      "$ref": "#/definitions/BindingMutation"
    },
    "RotateRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "ticket": {
          "$ref": "#/definitions/Ticket"
        },
        "reason": {
          "const": "manual"
        }
      },
      "required": [
        ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "ticket",
        "reason"
      ],
      "additionalProperties": false
    },
    "OperationQuery": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "operation": {
          "type": "string",
          "enum": [
            "open",
            "renew",
            "rotate",
            "close",
            "rebind"
          ]
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "operation",
        "request",
        "bindingId"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "operation": {
                "const": "open"
              }
            }
          },
          "then": {
            "properties": {
              "bindingId": {
                "type": "null"
              }
            }
          },
          "else": {
            "properties": {
              "bindingId": {
                "$ref": "#/definitions/Id"
              }
            }
          }
        }
      ]
    },
    "MutationReceipt": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "operation": {
          "type": "string",
          "enum": [
            "open",
            "renew",
            "rotate",
            "close",
            "rebind"
          ]
        },
        "semanticDigest": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "state": {
          "const": "completed"
        },
        "binding":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( {
          "$ref": "#/definitions/BindingView"
        },
        "ticket": {
          "anyOf": [
            {
              "$ref": "#/definitions/Ticket"
            },
            {
              "type": "null"
            }
          ]
        },
        "ticketExpiresAt": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "request",
        "operation",
        "semanticDigest",
        "state",
        "binding",
        "ticket",
        "ticketExpiresAt"
      ],
      "additionalProperties": false,
      "$comment": "Immutable original result, not current authorization or current binding state. Atomic mapping/ticket/revision/receipt commit. Replay under current authority precedes fresh epoch/CAS; never renew TTL on replay."
    },
    "ProviderDomain": {
      "type": "object",
      "properties)TANSR_SCHEMA",
    R"TANSR_SCHEMA(": {
        "provider": {
          "$ref": "#/definitions/Id"
        },
        "model": {
          "$ref": "#/definitions/LegacyId"
        },
        "protocol": {
          "type": "string",
          "enum": [
            "openai_chat",
            "openai_responses",
            "anthropic_messages"
          ]
        },
        "deploymentRef": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "adapterVersion": {
          "$ref": "#/definitions/Id"
        },
        "faceCacheRevision": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "provider",
        "model",
        "protocol",
        "deploymentRef",
        "adapterVersion",
        "faceCacheRevision"
      ],
      "additionalProperties": false
    },
    "PolicyWitness": {
      "oneOf": [
        {
          "$ref": "#/definitions/CorePolicyWitness"
        },
        {
          "$ref": "#/definitions/GatewayPolicyWitness"
        }
      ]
    },
    "PrefixSegment": {
      "typ)TANSR_SCHEMA",
    R"TANSR_SCHEMA(e": "object",
      "properties": {
        "index": {
          "type": "integer",
          "minimum": 0,
          "maximum": 63
        },
        "kind": {
          "type": "string",
          "enum": [
            "tools",
            "system",
            "summary",
            "recent-history",
            "current-input"
          ]
        },
        "cumulativeFingerprint": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "visibleBytes": {
          "type": "integer",
          "minimum": 0,
          "maximum": 33554432
        }
      },
      "required": [
        "index",
        "kind",
        "cumulativeFingerprint",
        "visibleBytes"
      ],
      "additionalProperties": false
    },
    "ProjectionWitness": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "logicalRef": {
          "$ref": "#/definitions/Id"
        },
        "projectionRef": {
          "$ref": "#/definitions/Id"
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
          "anyOf": [
            {
              "$ref": "#/definitions/Generations"
            },
            {
              "type": "null"
            }
          ]
        },
        "source": {
          "type": "object",
          "properties": {
            "kind": {
              "type": "string",
              "enum": [
                "session-store",
                "archive",
                "gateway-request"
              ]
            },
            "reference": {
              "$ref": "#/definitions/Id"
            },
            "effectiveSnapshot": {
              "$ref": "#/definitions/KeyedDigest"
            },
            "summaryCoverage": {
              "anyOf": [
                {
                  "$ref": "#/definitions/Coverage"
                },
                {
                  "type": "null"
                }
              ]
            }
          },
          "requi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(red": [
            "kind",
            "reference",
            "effectiveSnapshot",
            "summaryCoverage"
          ],
          "additionalProperties": false
        },
        "assemblerVersion": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "serializerVersion": {
          "$ref": "#/definitions/Id"
        },
        "provider": {
          "$ref": "#/definitions/ProviderDomain"
        },
        "policy": {
          "$ref": "#/definitions/PolicyWitness"
        },
        "segments": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/PrefixSegment"
          },
          "minItems": 0,
          "maxItems": 64
        },
        "requestFingerprint": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "gatewayInput": {
          "anyOf": [
            {
              "$ref": "#/definitions/GatewayInputWitness"
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "logicalRef",
        "projectionRef",
        "revision",
        "generations",
        "source",
        "assemblerVersion",
        "serializerVersion",
        "provider",
        "policy",
        "segments",
        "requestFingerprint",
        "gatewayInput"
      ],
      "additionalProperties": false,
      "$comment": "Trusted core/provider output only, not a public write request. Immutable effective projection reference, never raw-history reinsertion or external summary authority. HMAC inputs are original IR/provider bytes, not control JSON encoding. Domain/scope/key/context must match before comparison. Core and gateway witness domains are tagged and non-comparable. Gateway has no core generations/assembler/coverage. Its serializerVersion names the actual TWP parser/serializer contract, not a claimed core version.",
      "allOf": [
        {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "if": {
            "properties": {
              "source": {
                "properties": {
                  "kind": {
                    "const": "gateway-request"
                  }
                }
              }
            }
          },
          "then": {
            "properties": {
              "policy": {
                "$ref": "#/definitions/GatewayPolicyWitness"
              },
              "generations": {
                "type": "null"
              },
              "assemblerVersion": {
                "type": "null"
              },
              "gatewayInput": {
                "$ref": "#/definitions/GatewayInputWitness"
              },
              "source": {
                "properties": {
                  "summaryCoverage": {
                    "type": "null"
                  }
                }
              }
            }
          },
          "else": {
            "properties": {
              "policy": {
                "$ref": "#/definitions/CorePolicyWitness"
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(              },
              "generations": {
                "$ref": "#/definitions/Generations"
              },
              "assemblerVersion": {
                "$ref": "#/definitions/Id"
              },
              "gatewayInput": {
                "type": "null"
              }
            }
          }
        }
      ]
    },
    "ProjectionPublish": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "previousProjectionRef": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "projection": {
          "$ref": "#/definitions/ProjectionWitness"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "expect)TANSR_SCHEMA",
    R"TANSR_SCHEMA(edRevision",
        "previousProjectionRef",
        "projection"
      ],
      "additionalProperties": false,
      "$comment": "Internal synchronous prepare/persist/publish with current history/policy/source guard; no public route. Core acceptance and immutable projection reference commit together or remain unknown, never install stale projected state."
    },
    "ExchangeMetadata": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "ticket": {
          "$ref": "#/definitions/Ticket"
        },
        "runtimeSessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "projectionRef": {
          "$ref": "#/definitions/Id"
        },
        "payloadBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 33554432
        },
        "payloadSha256": {
          "$ref": "#/definitions/Digest"
        }
      },
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "required": [
        "protocol",
        "bindingId",
        "ticket",
        "runtimeSessionId",
        "projectionRef",
        "payloadBytes",
        "payloadSha256"
      ],
      "additionalProperties": false,
      "$comment": "Gateway route only: 4-byte big-endian metadata length + canonical metadata <=64KiB + unchanged original TWP UTF-8 body <=32MiB. SHA is raw transport integrity, never a diagnostic fingerprint. Existing TWP body schema and original auth/signature/idempotency/usage remain required; new operation semantic identity additionally binds this immutable envelope. Never decode/reencode inner bytes for retry."
    },
    "MappingRecord": {
      "type": "object",
      "properties": {
        "format": {
          "const": "sdk2-cache-mapping-v1"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "logicalRef": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(groupRef": {
          "$ref": "#/definitions/Id"
        },
        "groupGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "state": {
          "type": "string",
          "enum": [
            "active",
            "revoked",
            "expired"
          ]
        },
        "policy": {
          "$ref": "#/definitions/PolicyWitness"
        },
        "createdAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "lastAuthorizedUseAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "idleExpiresAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "absoluteExpiresAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "bindingIds": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Id"
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       },
          "minItems": 0,
          "maxItems": 16,
          "uniqueItems": true
        },
        "generations": {
          "anyOf": [
            {
              "$ref": "#/definitions/Generations"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "format",
        "scope",
        "logicalRef",
        "revision",
        "groupRef",
        "groupGeneration",
        "state",
        "policy",
        "createdAtMs",
        "lastAuthorizedUseAtMs",
        "idleExpiresAtMs",
        "absoluteExpiresAtMs",
        "bindingIds",
        "generations"
      ],
      "additionalProperties": false,
      "$comment": "Durable authoritative multi-instance CAS record; only mapping metadata, no prompts/provider credentials. Scope tuple+logicalRef unique, versioned transactions; cache/Redis is not authoritative. Terminal/ticket rows separately bounded and atomically linked. No runtime lease or billing identity substitution.)TANSR_SCHEMA",
    R"TANSR_SCHEMA(",
      "allOf": [
        {
          "if": {
            "properties": {
              "policy": {
                "properties": {
                  "kind": {
                    "const": "gateway-observed-v1"
                  }
                }
              }
            }
          },
          "then": {
            "properties": {
              "generations": {
                "type": "null"
              },
              "scope": {
                "properties": {
                  "authorizationRevision": {
                    "type": "null"
                  }
                }
              }
            }
          },
          "else": {
            "properties": {
              "generations": {
                "$ref": "#/definitions/Generations"
              },
              "scope": {
                "properties": {
                  "authorizationRevision": {
                    "$ref": "#/definitions/Sequence"
                  }
                }
              }
            }
          }
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      }
      ]
    },
    "Usage": {
      "type": "object",
      "properties": {
        "source": {
          "type": "string",
          "enum": [
            "provider-reported",
            "unavailable"
          ]
        },
        "cacheReadTokens": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            },
            {
              "type": "null"
            }
          ]
        },
        "cacheWriteTokens": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            },
            {
              "type": "null"
            }
          ]
        },
        "uncachedInputTokens": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            },
            {
              "type": "null"
            }
          ]
        },)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
        "outputTokens": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            },
            {
              "type": "null"
            }
          ]
        },
        "cacheState": {
          "type": "string",
          "enum": [
            "reported-read",
            "reported-no-read",
            "unknown"
          ]
        },
        "billingVerified": {
          "type": "boolean"
        }
      },
      "required": [
        "source",
        "cacheReadTokens",
        "cacheWriteTokens",
        "uncachedInputTokens",
        "outputTokens",
        "cacheState",
        "billingVerified"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "source": {
                "const": "unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "cacheReadTokens": {
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "type": "null"
              },
              "cacheWriteTokens": {
                "type": "null"
              },
              "uncachedInputTokens": {
                "type": "null"
              },
              "outputTokens": {
                "type": "null"
              },
              "cacheState": {
                "const": "unknown"
              },
              "billingVerified": {
                "const": false
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "cacheState": {
                "const": "reported-read"
              }
            }
          },
          "then": {
            "properties": {
              "source": {
                "const": "provider-reported"
              },
              "cacheReadTokens": {
                "type": "integer",
                "minimum": 1,
                "maximum": 9007199254740991
              }
            }
          }
        },
        {
          "if":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( {
            "properties": {
              "cacheState": {
                "const": "reported-no-read"
              }
            }
          },
          "then": {
            "properties": {
              "source": {
                "const": "provider-reported"
              },
              "cacheReadTokens": {
                "const": 0
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "cacheState": {
                "const": "unknown"
              }
            }
          },
          "then": {
            "properties": {
              "cacheReadTokens": {
                "type": "null"
              }
            }
          }
        }
      ]
    },
    "Diagnostic": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "diagnosticId": {
          "$ref": "#/definitions/Id"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
        "requestRef": {
          "$ref": "#/definitions/Id"
        },
        "groupGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "comparison": {
          "type": "string",
          "enum": [
            "same-prefix",
            "prefix-changed",
            "not-comparable"
          ]
        },
        "firstChangedSegment": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 63
            },
            {
              "type": "null"
            }
          ]
        },
        "cause": {
          "type": "string",
          "enum": [
            "first-request",
            "content-changed",
            "group-rotated",
            "policy-refreshed",
            "provider-domain-changed",
            "projection-missing",
            "key-rotated",
            "unsupported",
            "provider-reported",
            "unknown"
          ]
        },
        "causeAuthority": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "type": "string",
          "enum": [
            "local-fact",
            "provider-report",
            "unknown"
          ]
        },
        "usage": {
          "$ref": "#/definitions/Usage"
        },
        "elapsedMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 3600000
        },
        "retryCount": {
          "type": "integer",
          "minimum": 0,
          "maximum": 100
        },
        "observedAt": {
          "type": "string",
          "format": "date-time"
        }
      },
      "required": [
        "protocol",
        "diagnosticId",
        "bindingId",
        "requestRef",
        "groupGeneration",
        "comparison",
        "firstChangedSegment",
        "cause",
        "causeAuthority",
        "usage",
        "elapsedMs",
        "retryCount",
        "observedAt"
      ],
      "additionalProperties": false,
      "$comment": "No prompts, raw/HMAC fingerprints, raw app/user identities, tickets, raw groupRef, provider keys, reques)TANSR_SCHEMA",
    R"TANSR_SCHEMA(t body digests or unbounded messages. Correlating prefix equality is not a cache-hit assertion. Same-scope diagnostics permission and caps on every page/read."
    },
    "DiagnosticQuery": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "after": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "limit": {
          "type": "integer",
          "minimum": 1,
          "maximum": 100
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "after",
        "limit"
      ],
      "additionalProperties": false
    },
    "DiagnosticPage": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "rows": {
          "type": "arra)TANSR_SCHEMA",
    R"TANSR_SCHEMA(y",
          "items": {
            "$ref": "#/definitions/Diagnostic"
          },
          "minItems": 0,
          "maxItems": 100
        },
        "next": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "rows",
        "next"
      ],
      "additionalProperties": false
    },
    "ErrorResponse": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "code": {
          "type": "string",
          "enum": [
            "invalid_request",
            "protocol_version_mismatch",
            "unauthorized",
            "forbidden",
            "stale_revision",
            "stale_generation",
            "request_id_conflict",
            "projection_stale",
            "ticket_e)TANSR_SCHEMA",
    R"TANSR_SCHEMA(xpired",
            "mapping_expired",
            "receipt_expired",
            "payload_too_large",
            "unsupported_capability",
            "capacity_exceeded",
            "epoch_unavailable",
            "mapping_unavailable",
            "result_unknown"
          ]
        },
        "status": {
          "type": "integer",
          "minimum": 400,
          "maximum": 503
        },
        "retryAction": {
          "type": "string",
          "enum": [
            "none",
            "same-request",
            "query-status",
            "refresh-projection"
          ]
        },
        "fallback": {
          "type": "string",
          "enum": [
            "none",
            "legacy-cold"
          ]
        },
        "message": {
          "type": "string",
          "minLength": 1,
          "maxLength": 256
        }
      },
      "required": [
        "protocol",
        "requestId",
        "code",
        "status",
        "retryAction",
        "fallback",
        "messag)TANSR_SCHEMA",
    R"TANSR_SCHEMA(e"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "code": {
                "const": "invalid_request"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 400
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "protocol_version_mismatch"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 400
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "unauthorized"
              }
            }
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(          },
          "then": {
            "properties": {
              "status": {
                "const": 401
              },
              "retryAction": {
                "const": "none"
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "forbidden"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 403
              },
              "retryAction": {
                "const": "none"
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stale_revision"
              }
            }
          },
          "then": {
            "properties": {
            )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "status": {
                "const": 409
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stale_generation"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "request_id_conflict"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "const": "none"
              },
              "fallback": {
                "const": "none"
              }
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "projection_stale"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "ticket_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 410
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "mapping_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 410
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "receipt_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 410
              },
              "retryAction": {
                "const": "none"
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "payload_too_large"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 413
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
             )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "const": "unsupported_capability"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 422
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capacity_exceeded"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [429, 503]
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "epoch_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [409, 503]
              },
              "fallback": {
                "const": "none"
              }
           )TANSR_SCHEMA",
    R"TANSR_SCHEMA( }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "mapping_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [404, 409, 503]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": { "const": "mapping_unavailable" },
              "status": { "enum": [404, 409] }
            }
          },
          "then": {
            "properties": {
              "retryAction": { "const": "none" },
              "fallback": { "const": "none" }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "result_unknown"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 503
              },
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "retryAction": {
                "const": "query-status"
              },
              "fallback": {
                "const": "none"
              }
            }
          }
        }
      ]
    },
    "RebindRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "sessionId"
      ],
      "additionalProperties": false,
      "$comment": "Current owner authentication plus trusted persisted session/logical lineage, not a bare claimed sessionId; may replace expired/rotated device ticket only wh)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ile mapping and binding remain active and current policy/deletion witnesses match. No resurrection of revoked/closed/expired mapping."
    },
    "RecordSequence": {
      "allOf": [
        {
          "$ref": "#/definitions/Sequence"
        },
        {
          "not": {
            "const": "0"
          }
        }
      ],
      "$comment": "Record ordinals start at 1. Zero is only an empty published/acknowledged watermark, never a record ordinal."
    },
    "PolicyRefresh": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "protocol": {
          "$ref": "#/definitions/Protocol"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "policy": {
          "$ref": "#/definitions/PolicyWitness"
        },
        "generations": {
          "anyOf": [
            {
              "$ref": "#/definitions/Generations"
            },
            {
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "type": "null"
            }
          ]
        },
        "action": {
          "enum": [
            "rotate",
            "revoke"
          ]
        },
        "reason": {
          "enum": [
            "prompt-changed",
            "permission-changed",
            "capability-changed",
            "memory-changed",
            "tools-changed",
            "source-changed",
            "deletion-changed",
            "authorization-changed",
            "gateway-observation-changed"
          ]
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "expectedRevision",
        "policy",
        "generations",
        "action",
        "reason"
      ],
      "$comment": "Trusted host-only current authority/source facts; never public input. Same-transaction CAS rotates group generation and revokes all tickets, or revokes mapping. Source/deletion/authorization boundary changes require revoke; a harmless still-authorized policy refresh may rotate then permit li)TANSR_SCHEMA",
    R"TANSR_SCHEMA(neage-verified rebind. No ticket alone can authorize this transition. Gateway observations update only the same witness domain with a strictly newer observationRevision. Authorization boundary change still revokes. Observation change rotates without claiming a core prompt/memory/source generation. Core policy refresh follows existing logical-turn and governance publication barriers.",
      "allOf": [
        {
          "if": {
            "properties": {
              "reason": {
                "enum": [
                  "source-changed",
                  "deletion-changed",
                  "authorization-changed"
                ]
              }
            }
          },
          "then": {
            "properties": {
              "action": {
                "const": "revoke"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "policy": {
                "properties": {
                  "kind": {
                    "const": "g)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ateway-observed-v1"
                  }
                }
              }
            }
          },
          "then": {
            "properties": {
              "generations": {
                "type": "null"
              }
            }
          },
          "else": {
            "properties": {
              "generations": {
                "$ref": "#/definitions/Generations"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "policy": {
                "properties": {
                  "kind": {
                    "const": "gateway-observed-v1"
                  }
                }
              }
            }
          },
          "then": {
            "properties": {
              "reason": {
                "enum": [
                  "gateway-observation-changed",
                  "authorization-changed"
                ]
              }
            }
          },
          "else": {
            "properties": {
            )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "reason": {
                "not": {
                  "const": "gateway-observation-changed"
                }
              }
            }
          }
        }
      ]
    },
    "GatewayLink": {
      "type": "object",
      "additionalProperties": false,
      "properties": {
        "format": {
          "const": "sdk2-cache-gateway-link-v1"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "localBindingId": {
          "$ref": "#/definitions/Id"
        },
        "state": {
          "enum": [
            "pending",
            "active",
            "closing",
            "closed",
            "reconciliation-required"
          ]
        },
        "gatewayBindingId": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "gatewayLogicalRef": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           {
              "type": "null"
            }
          ]
        },
        "gatewayRevision": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "protectedTicketRef": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "operation": {
          "enum": [
            "open",
            "renew",
            "rotate",
            "close",
            "rebind"
          ]
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "protectedIntentRef": {
          "$ref": "#/definitions/Id"
        },
        "intentDigest": {
          "$ref": "#/definitions/KeyedDigest"
        }
      },
      "required": [
        "format",
        "scope",
        "localBindingId",
        "state",
        "gatew)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ayBindingId",
        "gatewayLogicalRef",
        "gatewayRevision",
        "protectedTicketRef",
        "operation",
        "request",
        "protectedIntentRef",
        "intentDigest"
      ],
      "$comment": "Host-only durable outbox link. Reserve local key and exact protected gateway intent before remote mutation; current auth remains separate. Query/replay same remote key after unknown outcome; never transfer the public serve audience ticket to gateway. Active requires known committed remote binding, revision and protected gateway ticket. Counts toward all retained-state budgets; pending is never TTL-GCed.",
      "allOf": [
        {
          "if": {
            "properties": {
              "state": {
                "const": "active"
              }
            }
          },
          "then": {
            "properties": {
              "gatewayBindingId": {
                "$ref": "#/definitions/Id"
              },
              "gatewayLogicalRef": {
                "$ref": "#/definitions)TANSR_SCHEMA",
    R"TANSR_SCHEMA(/Id"
              },
              "gatewayRevision": {
                "$ref": "#/definitions/Sequence"
              },
              "protectedTicketRef": {
                "$ref": "#/definitions/Id"
              }
            }
          }
        }
      ]
    },
    "CorePolicyWitness": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "core-effective-v1"
        },
        "authorizationRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "promptRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "capabilityRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "permissionRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "memoryRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "toolSetRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "systemLayerPolicyRevision": {
          "$ref": "#/definitions/Sequence"
        }
      },
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "required": [
        "kind",
        "authorizationRevision",
        "promptRevision",
        "capabilityRevision",
        "permissionRevision",
        "memoryRevision",
        "toolSetRevision",
        "systemLayerPolicyRevision"
      ],
      "additionalProperties": false,
      "$comment": "Trusted core effective logical-turn revisions only. No gateway inference, placeholder revision, mid-turn system refresh or public write authority."
    },
    "GatewayPolicyWitness": {
      "type": "object",
      "properties": {
        "kind": {
          "const": "gateway-observed-v1"
        },
        "observationRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "authorizationSnapshot": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "platformConfigSnapshot": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "corePolicy": {
          "type": "null"
        }
      },
      "required": [
        "kind",
        "observationRevision",
        "a)TANSR_SCHEMA",
    R"TANSR_SCHEMA(uthorizationSnapshot",
        "platformConfigSnapshot",
        "corePolicy"
      ],
      "additionalProperties": false,
      "$comment": "Gateway-owned durable observation CAS revision, not any core revision. HMACs witness currently observed authorized platform facts but never grant authority. Core revisions are explicitly unknown; a separately authenticated delegation contract is not provided by this candidate."
    },
    "GatewayInputWitness": {
      "type": "object",
      "properties": {
        "originalTwp": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "effectiveSystem": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "effectiveTools": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "sanitizedParameters": {
          "$ref": "#/definitions/KeyedDigest"
        },
        "finalProviderRequest": {
          "$ref": "#/definitions/KeyedDigest"
        }
      },
      "required": [
        "originalTwp",
        "effectiveSystem",
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA("effectiveTools",
        "sanitizedParameters",
        "finalProviderRequest"
      ],
      "additionalProperties": false,
      "$comment": "Computed by the trusted gateway from validated original input and actual final member request bytes. No control JSON normalization of IR/parameters. Current platform configuration is a separate GatewayPolicyWitness fact; it cannot replace the effective system in the fixed logical turn."
    },
    "FinancialAnchor": {
      "type": "object",
      "properties": {
        "ownerType": {
          "enum": [
            "user",
            "org"
          ]
        },
        "ownerId": {
          "$ref": "#/definitions/LegacyId"
        },
        "requestId": {
          "type": "string",
          "minLength": 12,
          "maxLength": 44,
          "pattern": "^twp:[A-Za-z0-9_-]{8,40}$"
        }
      },
      "required": [
        "ownerType",
        "ownerId",
        "requestId"
      ],
      "additionalProperties": false,
      "$comment": "Trusted old fina)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ncial identity: (owner_type, owner_id, twp:<original inner.meta.requestId>). Never derive a replacement from app/user/runtime/envelope or cache/HMAC key versions. This DTO does not create a new ledger."
    },
    "GatewayAdmissionIdentity": {
      "type": "object",
      "properties": {
        "financialAnchor": {
          "$ref": "#/definitions/FinancialAnchor"
        },
        "scope": {
          "type": "object",
          "properties": {
            "applicationScopeId": {
              "$ref": "#/definitions/Id"
            },
            "endUserId": {
              "$ref": "#/definitions/LegacyId"
            }
          },
          "required": [
            "applicationScopeId",
            "endUserId"
          ],
          "additionalProperties": false
        },
        "audience": {
          "const": "gateway-cache"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "runtimeSessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(projectionRef": {
          "$ref": "#/definitions/Id"
        },
        "innerRequestId": {
          "type": "string",
          "minLength": 8,
          "maxLength": 40,
          "pattern": "^[A-Za-z0-9_-]+$"
        },
        "innerBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 33554432
        },
        "innerSha256": {
          "$ref": "#/definitions/Digest"
        },
        "envelopeBytes": {
          "type": "integer",
          "minimum": 6,
          "maximum": 33619972
        },
        "envelopeSha256": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "financialAnchor",
        "scope",
        "audience",
        "bindingId",
        "runtimeSessionId",
        "projectionRef",
        "innerRequestId",
        "innerBytes",
        "innerSha256",
        "envelopeBytes",
        "envelopeSha256"
      ],
      "additionalProperties": false,
      "$comment": "Host-only exact witnessed identity. Runtime must d)TANSR_SCHEMA",
    R"TANSR_SCHEMA(erive innerRequestId from the validated original TWP, require financialAnchor.requestId === twp: + innerRequestId, derive scope and owner from current authentication, and validate all lengths/SHA against the full frame. Never accept client hashes as authority. Public ExchangeMetadata is unchanged."
    },
    "GatewayAdmissionRecord": {
      "type": "object",
      "properties": {
        "format": {
          "const": "sdk2-cache-admission-v1"
        },
        "identity": {
          "$ref": "#/definitions/GatewayAdmissionIdentity"
        },
        "admittedAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "idleExpiresAtMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "financialDisposition": {
          "enum": [
            "existing-reservation",
            "not-required"
          ]
        },
        "state": {
          "enum": [
            "accepted",
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "result-unknown",
            "completed"
          ]
        },
        "resultRef": {
          "anyOf": [
            {
              "$ref": "#/definitions/Id"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "format",
        "identity",
        "admittedAtMs",
        "idleExpiresAtMs",
        "financialDisposition",
        "state",
        "resultRef"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "state": {
                "const": "completed"
              }
            }
          },
          "then": {
            "properties": {
              "resultRef": {
                "$ref": "#/definitions/Id"
              }
            }
          },
          "else": {
            "properties": {
              "resultRef": {
                "type": "null"
              }
            }
          }
        }
      ],
      "$comment": "D)TANSR_SCHEMA",
    R"TANSR_SCHEMA(urable non-financial admission anchor unique by the original FinancialAnchor. not-required means the original gateway confirms no reservation at all, not merely zero cash. Positive gross covered by gifted Tokens still requires existing-reservation with its original token_state/hold, even if reservation.amountMicro is zero. First anchor + any original reservation/Token hold + mapping idle touch commit atomically on one DB connection; Redis is not this authority. Existing accepted/unknown records are historical facts only, never provider execution permits. Pending cannot be GCed; terminal retention must keep a bounded denial anchor while original IDs could be submitted again."
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_4[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://schemas.tansr.com/drafts/sdk2-ext-v1.schema.json",
  "title": "SDK2 extension v1 protocol: SDK1-compatible archive and execution-boundary overlays",
  "description": "SDK2 extension v1 is an additive protocol surface. SDK2-10 adds an opt-in execution boundary view; existing SDK1 API, SessionStore, IR, SSE, cache behavior and frozen protocol contracts remain unchanged. HTTP handlers validate their named definition, not the root union.",
  "$comment": "Route namespace /v3/sdk2. Schema validates one DTO, never authenticity, authorization, UTF-8 total bytes, referenced IR payload semantics or persistent CAS/state. Those are mandatory normative checks in RFC-SDK2-1 and the contract fixtures; passing this schema is not product acceptance. New requests reject unknown keys. Referenced body attachments use controlled source/ID access only. No host-control or arbitrary filesystem/URL data source exists.",
  "anyOf": [
    {
      "$ref": "#/def)TANSR_SCHEMA",
    R"TANSR_SCHEMA(initions/CapabilitiesResponse"
    },
    {
      "$ref": "#/definitions/BindingCreateRequest"
    },
    {
      "$ref": "#/definitions/BindingView"
    },
    {
      "$ref": "#/definitions/ArchiveReadRequest"
    },
    {
      "$ref": "#/definitions/ArchivePage"
    },
    {
      "$ref": "#/definitions/ArtifactReadRequest"
    },
    {
      "$ref": "#/definitions/ArtifactChunk"
    },
    {
      "$ref": "#/definitions/ArchiveAckRequest"
    },
    {
      "$ref": "#/definitions/ArchiveStatus"
    },
    {
      "$ref": "#/definitions/MaterialRequest"
    },
    {
      "$ref": "#/definitions/MaterialResponseRequest"
    },
    {
      "$ref": "#/definitions/MaterialReceipt"
    },
    {
      "$ref": "#/definitions/BindingCloseRequest"
    },
    {
      "$ref": "#/definitions/MutationReceipt"
    },
    {
      "$ref": "#/definitions/OperationStatusRequest"
    },
    {
      "$ref": "#/definitions/EventFrame"
    },
    {
      "$ref": "#/definitions/ErrorResponse"
    },
    {
      "$ref": "#/defin)TANSR_SCHEMA",
    R"TANSR_SCHEMA(itions/BindingTargetRequest"
    },
    {
      "$ref": "#/definitions/BindingTargetView"
    },
    {
      "$ref": "#/definitions/MaterialUploadChunkRequest"
    },
    {
      "$ref": "#/definitions/MaterialUploadStatusRequest"
    },
    {
      "$ref": "#/definitions/MaterialUploadStatus"
    },
    {
      "$ref": "#/definitions/MaterialStatusRequest"
    },
    {
      "$ref": "#/definitions/ExecutionBoundary"
    }
  ],
  "definitions": {
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Existing opaque ID; no trimming, case folding or Unicode normalization. Do not mint authority from this string."
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12)TANSR_SCHEMA",
    R"TANSR_SCHEMA(}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "Capability": {
      "type": "string",
      "enum": [
        "archive-transfer-v1",
        "context-materials-v1"
      ]
    },
    "Scope": {
      "type": "object",
      "properties": {
        "applicationScopeId": {
          "$ref": "#/definitions/Id"
        },
        "endUserId": {
          "$ref": "#/definitions/LegacyId"
        },
        "authorizationRevision": {
          "$ref": "#/definitions/Sequence"
        }
      },
      "required": [
        "applicationScopeId",
        "endUserId",
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA("authorizationRevision"
      ],
      "additionalProperties": false,
      "$comment": "Response/host-adapter only. Client requests have no Scope field. Reauthenticate and authorize every operation; archive roles never imply approve or execute."
    },
    "RequestIdentity": {
      "type": "object",
      "properties": {
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "operationEpoch": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "requestId",
        "operationEpoch"
      ],
      "additionalProperties": false,
      "$comment": "Epoch is issued and retired by the trusted host. Same scope/resource/operation/epoch/requestId and semantic content returns its original receipt, different content conflicts. Retired epochs never become fresh; unknown or expired results require query-status, not a new operation. Semantic digest uses SHA256(UTF8(tansr.sdk2.operation.v1)+NUL+canonical({scope:[trustedApp,trustedUser],operation,semantic})), semant)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ic is immutable request body excluding request identity and transport/auth envelope. expectedRevision belongs to semantic content. Fresh auth/epoch validation gates NEW admission; existing-operation status remains queryable under current permission after epoch retirement."
    },
    "Generations": {
      "type": "object",
      "properties": {
        "historyEpoch": {
          "$ref": "#/definitions/LegacyId"
        },
        "deletionGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "projectionRevision": {
          "$ref": "#/definitions/Sequence"
        }
      },
      "required": [
        "historyEpoch",
        "deletionGeneration",
        "projectionRevision"
      ],
      "additionalProperties": false
    },
    "Target": {
      "type": "object",
      "properties": {
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "sourceSnapshotDigest": {
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "sessionId",
        "generations",
        "sourceSnapshotDigest"
      ],
      "additionalProperties": false,
      "$comment": "Read-only host observation of current effective history. sourceSnapshotDigest=SHA256(UTF8(tansr.sdk2.source-snapshot.v1)+NUL+snapshotBytes). Trusted host fixes snapshotBytes with the existing SDK1 IR serializer under the session exclusion boundary; do not reencode IR with the new control-metadata canonicalizer or strip/normalize floats, opaque values or provider-owned data to compute this witness. It is only a CAS witness, never client-provided authority or permission to export hidden content. Binding POST recomputes under the same boundary as send/rewrite/restore; unchanged extension revision alone cannot prove history unchanged."
    },
    "Limits": {
      "type": "object",
      "properties": {
        "controlBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: 262144,
          "default": 262144
        },
        "recordBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 262144,
          "default": 262144
        },
        "pageRecords": {
          "type": "integer",
          "minimum": 1,
          "maximum": 128,
          "default": 128
        },
        "pageBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 1048576,
          "default": 1048576
        },
        "attachmentBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 33554432,
          "default": 33554432
        },
        "chunkBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 262144,
          "default": 262144
        },
        "materialConcurrent": {
          "type": "integer",
          "minimum": 1,
          "maximum": 2,
          "default": 2
        },
        "materialQueue": {
          "type": "integer",
          "minimum": 1,
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "maximum": 16,
          "default": 16
        },
        "materialCandidates": {
          "type": "integer",
          "minimum": 1,
          "maximum": 32,
          "default": 32
        },
        "materialBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 1048576,
          "default": 1048576
        },
        "materialDeadlineMs": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 30000,
          "default": 30000
        },
        "pendingRecords": {
          "type": "integer",
          "minimum": 1,
          "maximum": 4096,
          "default": 4096
        },
        "pendingBytes": {
          "type": "integer",
          "minimum": 1048576,
          "maximum": 67108864,
          "default": 67108864
        },
        "inflightReserveBytes": {
          "type": "integer",
          "minimum": 1048576,
          "maximum": 16777216,
          "default": 16777216
        },
        "offlineMs": {
          "type": "integer",
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        "minimum": 1000,
          "maximum": 86400000,
          "default": 86400000
        },
        "eventRetentionMs": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 600000,
          "default": 600000
        },
        "eventRetentionFrames": {
          "type": "integer",
          "minimum": 1,
          "maximum": 4096,
          "default": 4096
        },
        "eventRetentionBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 8388608,
          "default": 8388608
        },
        "terminalReceiptRetentionMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 604800000,
          "default": 604800000
        },
        "epochLifetimeMs": {
          "type": "integer",
          "minimum": 60000,
          "maximum": 86400000,
          "default": 3600000
        },
        "materialChunkBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536,
          "default": 6)TANSR_SCHEMA",
    R"TANSR_SCHEMA(5536
        }
      },
      "required": [
        "controlBytes",
        "recordBytes",
        "pageRecords",
        "pageBytes",
        "attachmentBytes",
        "chunkBytes",
        "materialConcurrent",
        "materialQueue",
        "materialCandidates",
        "materialBytes",
        "materialDeadlineMs",
        "pendingRecords",
        "pendingBytes",
        "inflightReserveBytes",
        "offlineMs",
        "eventRetentionMs",
        "eventRetentionFrames",
        "eventRetentionBytes",
        "terminalReceiptRetentionMs",
        "epochLifetimeMs",
        "materialChunkBytes"
      ],
      "additionalProperties": false,
      "$comment": "UTF-8 decoded bytes, not character counts. Apply per-binding AND host/app/user quotas. Reserve must be <= pendingBytes and cover admitted inflight results. Pending records cannot be evicted by receipt/event/offline TTL; stop new admission and drain safely. These caps never modify legacy SessionStore retention or old v2 limits."
    },
    "Capab)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ilitiesResponse": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "availability": {
          "const": "legacy-complete"
        },
        "capabilities": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Capability"
          },
          "minItems": 1,
          "maxItems": 2,
          "uniqueItems": true
        },
        "limits": {
          "$ref": "#/definitions/Limits"
        },
        "operationEpoch": {
          "$ref": "#/definitions/EpochView"
        },
        "archiveAckFormats": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveAckFormat"
          },
          "minItems": 0,
          "maxItems": 1,
          "uniqueItems": true
        }
      },
      "required": [
        "protocol",
        "availability",
        "capabilities",
        "limits",
        "operationEpoch",
        "archiveAckFormats"
      ],
      "additionalProperti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(es": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "capabilities": {
                "contains": {
                  "const": "archive-transfer-v1"
                }
              }
            }
          },
          "then": {
            "properties": {
              "archiveAckFormats": {
                "minItems": 1
              }
            }
          },
          "else": {
            "properties": {
              "archiveAckFormats": {
                "maxItems": 0
              }
            }
          }
        }
      ],
      "$comment": "archiveAckFormats is exact discovery support, not authorization. No archive capability means an empty list. Binding selects one recognized format; never infer support from protocol family alone."
    },
    "BindingCreateRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity)TANSR_SCHEMA",
    R"TANSR_SCHEMA("
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "requiredCapabilities": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Capability"
          },
          "minItems": 1,
          "maxItems": 2,
          "uniqueItems": true
        },
        "optionalCapabilities": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Capability"
          },
          "minItems": 0,
          "maxItems": 2,
          "uniqueItems": true
        },
        "archive": {
          "type": "object",
          "properties": {
            "strategy": {
              "const": "single-authorized-source"
            },
            "sourceId": {
              "$ref": "#/definitions/Id"
            },
            "durability": {
              "const": "source-ack-with-durable-spool"
            },
            "delivery": {
              "co)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nst": "required"
            },
            "sessionAvailability": {
              "const": "legacy-complete"
            },
            "ackFormat": {
              "$ref": "#/definitions/ArchiveAckFormat"
            }
          },
          "required": [
            "strategy",
            "sourceId",
            "durability",
            "delivery",
            "sessionAvailability",
            "ackFormat"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "protocol",
        "request",
        "target",
        "expectedRevision",
        "requiredCapabilities",
        "optionalCapabilities",
        "archive"
      ],
      "additionalProperties": false,
      "$comment": "Only idle safe-point bind. Expected revision is the trusted per-session extension slot revision, initially 0. Required/optional capability sets must be disjoint; unavailable required capability is atomic failure. Source must be host-registered for principal; request never carries a pa)TANSR_SCHEMA",
    R"TANSR_SCHEMA(th, module, connector URL or provider credential. archive.ackFormat explicitly selects the only recognized draft format. If archive is required, unsupported format/capability fails atomically; optional archive rejection must not suppress separately accepted context-materials capability. Before required delivery, negotiated controlBytes must support a whole-record split ACK with the actual identity envelope and permitted artifact references.",
      "allOf": [
        {
          "not": {
            "type": "object",
            "properties": {
              "requiredCapabilities": {
                "contains": {
                  "const": "archive-transfer-v1"
                }
              },
              "optionalCapabilities": {
                "contains": {
                  "const": "archive-transfer-v1"
                }
              }
            }
          }
        },
        {
          "not": {
            "type": "object",
            "properties": {
              "requiredCapabilities": {
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(              "contains": {
                  "const": "context-materials-v1"
                }
              },
              "optionalCapabilities": {
                "contains": {
                  "const": "context-materials-v1"
                }
              }
            }
          }
        }
      ]
    },
    "BindingState": {
      "type": "string",
      "enum": [
        "active",
        "backpressured",
        "closing",
        "closed"
      ]
    },
    "BindingView": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "state": {
          "$ref": "#/definitions/BindingState"
        },
        "sourceId": {
          "$ref": "#/definiti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ons/Id"
        },
        "acceptedCapabilities": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Capability"
          },
          "minItems": 1,
          "maxItems": 2,
          "uniqueItems": true
        },
        "rejectedCapabilities": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "capability": {
                "$ref": "#/definitions/Capability"
              },
              "reason": {
                "type": "string",
                "enum": [
                  "unsupported_capability",
                  "not_authorized"
                ]
              }
            },
            "required": [
              "capability",
              "reason"
            ],
            "additionalProperties": false
          },
          "minItems": 0,
          "maxItems": 2
        },
        "availability": {
          "const": "legacy-complete"
        },
        "operationEpoch": {
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(anyOf": [
            {
              "$ref": "#/definitions/EpochView"
            },
            {
              "type": "null"
            }
          ]
        },
        "limits": {
          "$ref": "#/definitions/Limits"
        },
        "archiveAckFormat": {
          "anyOf": [
            {
              "$ref": "#/definitions/ArchiveAckFormat"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "scope",
        "target",
        "revision",
        "state",
        "sourceId",
        "acceptedCapabilities",
        "rejectedCapabilities",
        "availability",
        "operationEpoch",
        "limits",
        "archiveAckFormat"
      ],
      "additionalProperties": false,
      "$comment": "Read/query remains available without an accepting epoch. operationEpoch=null means no new write admission, not loss of stored receipts or current legacy history. archiveAckFormat is the)TANSR_SCHEMA",
    R"TANSR_SCHEMA( immutable selected format iff archive-transfer-v1 was accepted; otherwise null. It must match discovery and the create request.",
      "allOf": [
        {
          "if": {
            "properties": {
              "acceptedCapabilities": {
                "contains": {
                  "const": "archive-transfer-v1"
                }
              }
            }
          },
          "then": {
            "properties": {
              "archiveAckFormat": {
                "$ref": "#/definitions/ArchiveAckFormat"
              }
            }
          },
          "else": {
            "properties": {
              "archiveAckFormat": {
                "type": "null"
              }
            }
          }
        }
      ]
    },
    "ArtifactRef": {
      "type": "object",
      "properties": {
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "bytes": {
          "type": "integer",
          "minimu)TANSR_SCHEMA",
    R"TANSR_SCHEMA(m": 1,
          "maximum": 33554432
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        },
        "mediaType": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "pattern": "^[A-Za-z0-9][A-Za-z0-9!#$&^_.+/-]*$"
        }
      },
      "required": [
        "artifactId",
        "sourceId",
        "bytes",
        "sha256",
        "mediaType"
      ],
      "additionalProperties": false
    },
    "Coverage": {
      "type": "object",
      "properties": {
        "fromSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "throughSequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "headDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "fromSequence",
        "throughSequence",
        "headDigest"
      ],
      "additionalProperties": false,
      "$comment": "Closed inclusive interval, from<=through. Head is a host-known contiguou)TANSR_SCHEMA",
    R"TANSR_SCHEMA(s chain commitment for this source and generation, not a client assertion. SSE cursor is never archive coverage."
    },
    "ArchiveRecord": {
      "type": "object",
      "properties": {
        "recordId": {
          "$ref": "#/definitions/Id"
        },
        "sequence": {
          "$ref": "#/definitions/RecordSequence"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "turnId": {
          "$ref": "#/definitions/LegacyId"
        },
        "recordKind": {
          "type": "string",
          "enum": [
            "turn",
            "projection",
            "terminal"
          ]
        },
        "turnState": {
          "type": "string",
          "enum": [
            "completed",
            "cancelled",
            "incomplete"
          ]
        },
        "predecessorDigest": {
          "$ref": "#/definitions/Digest"
        },
        "recordDigest": {
          "$ref": "#/definitions/Digest"
        },
        "payload": {
          "$ref": "#/)TANSR_SCHEMA",
    R"TANSR_SCHEMA(definitions/ArtifactRef"
        },
        "attachments": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArtifactRef"
          },
          "minItems": 0,
          "maxItems": 128
        },
        "sourceEventRange": {
          "type": "object",
          "properties": {
            "firstSeq": {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            },
            "lastSeq": {
              "type": "integer",
              "minimum": 0,
              "maximum": 9007199254740991
            }
          },
          "required": [
            "firstSeq",
            "lastSeq"
          ],
          "additionalProperties": false
        },
        "projection": {
          "type": "object",
          "properties": {
            "coverage": {
              "$ref": "#/definitions/Coverage"
            },
            "assemblerVersion": {
              "$ref": "#/definitions/Id"
            },
            "source)TANSR_SCHEMA",
    R"TANSR_SCHEMA(HeadDigest": {
              "$ref": "#/definitions/Digest"
            }
          },
          "required": [
            "coverage",
            "assemblerVersion",
            "sourceHeadDigest"
          ],
          "additionalProperties": false
        },
        "payloadDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "recordId",
        "sequence",
        "target",
        "turnId",
        "recordKind",
        "turnState",
        "predecessorDigest",
        "recordDigest",
        "payload",
        "attachments",
        "payloadDigest"
      ],
      "additionalProperties": false,
      "$comment": "Only the core capture chain publishes authority. Payload preserves immutable original bytes from the frozen SDK1 IR serializer or the agreed core projection format; it is not reencoded or constrained by the new control-metadata canonical subset, referenced without filesystem paths/URLs. Decode through that frozen schema and validate complete tool pai)TANSR_SCHEMA",
    R"TANSR_SCHEMA(rs and lifecycle before core use. recordDigest covers immutable envelope+payload/attachment witnesses, excluding transport cursor. Projection fields are required exactly for recordKind=projection. ArtifactRef.sha256 is raw-byte integrity only. payloadDigest=SHA256(UTF8(tansr.sdk2.payload.v1)+NUL+payloadBytes). recordDigest=SHA256(UTF8(tansr.sdk2.record.v1)+NUL+canonical(record excluding recordDigest)); predecessorDigest links prior host recordDigest, first predecessor is 64 zeroes. Record metadata <=recordBytes, referenced payload and attachments use separate attachmentBytes/chunkBytes caps. Host capture removes non-deliverable hidden reasoning/secrets before this commitment; consumer must never reinterpret opaque provider payload as prompt authority.",
      "allOf": [
        {
          "if": {
            "properties": {
              "recordKind": {
                "const": "projection"
              }
            }
          },
          "then": {
            "required": [
              "projection"
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         ]
          },
          "else": {
            "not": {
              "required": [
                "projection"
              ]
            }
          }
        }
      ]
    },
    "ArchiveReadRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "afterSequence": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "limit": {
          "type": "integer",
          "minimum": 1,
          "maximum": 128
        },
        "maxBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 1048576
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "generations",
        "afterSequen)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ce",
        "limit",
        "maxBytes"
      ],
      "additionalProperties": false
    },
    "ArchivePage": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "records": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveRecord"
          },
          "minItems": 0,
          "maxItems": 128
        },
        "nextAfterSequence": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "complete": {
          "type": "boolean"
        },
        "publishedThroughSequence": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        }
          ]
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "generations",
        "records",
        "nextAfterSequence",
        "complete",
        "publishedThroughSequence"
      ],
      "additionalProperties": false,
      "$comment": "Pages carry whole record metadata; referenced payload is read separately in bounded chunks. Null afterSequence means before first record, never missing data. Records ordered and continuous from requested position. complete indicates snapshot page completion only, not future archive completion. A gap is an ErrorResponse, never an empty success page."
    },
    "ArtifactReadRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "offset": {
          "type": "integer",
          "minimum": 0,
          "maximum": 33554431
        },
        "maxBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 262144
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "generations",
        "artifactId",
        "offset",
        "maxBytes"
      ],
      "additionalProperties": false
    },
    "ArtifactChunk": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "offset": {
          "type": "integer",
          "minimum": 0,
          "maximum": 33554431
        },
        "bytes": {
          "type": "integer",
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "minimum": 1,
          "maximum": 262144
        },
        "totalBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 33554432
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        },
        "chunkSha256": {
          "$ref": "#/definitions/Digest"
        },
        "base64": {
          "type": "string",
          "minLength": 4,
          "maxLength": 349528,
          "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "artifactId",
        "sourceId",
        "generations",
        "offset",
        "bytes",
        "totalBytes",
        "sha256",
        "chunkSha256",
        "base64"
      ],
      "additionalProperties": false,
      "$comment": "Validate decoded length, canonical base64, offset+bytes<=totalBytes, chunk hash and final complete artifact hash. Reference is ACL-scoped, not a bearer or URL. No general outbound fet)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ch or host path resolver."
    },
    "ArchiveAckRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Id"
        },
        "coverage": {
          "$ref": "#/definitions/Coverage"
        },
        "attachments": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveArtifactReceipt"
          },
          "minItems": 0,
          "maxItems": 128,
          "uniqueItems": true
        },
        "ackFormat": {
          "$ref": "#/definitions/ArchiveAckForm)TANSR_SCHEMA",
    R"TANSR_SCHEMA(at"
        },
        "payloads": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ArchiveArtifactReceipt"
          },
          "minItems": 1,
          "maxItems": 128,
          "uniqueItems": true
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "generations",
        "sourceId",
        "sourceGeneration",
        "coverage",
        "attachments",
        "ackFormat",
        "payloads"
      ],
      "additionalProperties": false,
      "$comment": "split-receipts-v1 only: payloads and extra attachments are separate exact first-occurrence receipt sets over the entire closed coverage (including overlap), at most 128 whole contiguous records and at most 128 receipts per category. Same artifact in both categories requires matching witnesses but physical accounting occurs once. Check raw request/control UTF-8 byte limit plus canonical envelope before side effects. Source atomically co)TANSR_SCHEMA",
    R"TANSR_SCHEMA(mmits record metadata, body, attachments, contiguous head and original ACK outbox intent before claiming durably-stored. Current authorization then original same-key semantic receipt precedes new epoch/CAS/generation checks. New ACK atomically commits fact, watermark, revision and receipt, never partial records. ACK alone does not release dependencies, old SessionStore, or authorize core execution."
    },
    "ArchiveStatus": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Id"
        },
        "publishedThroughSequence": {
          "anyOf": [
            {
              "$ref": "#/defi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "acknowledgedCoverage": {
          "anyOf": [
            {
              "$ref": "#/definitions/Coverage"
            },
            {
              "type": "null"
            }
          ]
        },
        "releasableThroughSequence": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "pendingBytes": {
          "type": "integer",
          "minimum": 0,
          "maximum": 67108864
        },
        "pendingRecords": {
          "type": "integer",
          "minimum": 0,
          "maximum": 4096
        },
        "sessionPersistence": {
          "const": "unchanged"
        },
        "state": {
          "$ref": "#/definitions/BindingState"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "revision",
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "generations",
        "sourceId",
        "sourceGeneration",
        "publishedThroughSequence",
        "acknowledgedCoverage",
        "releasableThroughSequence",
        "pendingBytes",
        "pendingRecords",
        "sessionPersistence",
        "state"
      ],
      "additionalProperties": false,
      "$comment": "ACK <= published head; releasable <= ACK and rewrite/active-run dependencies. Current effective legacy history is never released by archive ACK. Old Store commit and archive status are independent; no cross-store success substitution."
    },
    "MaterialRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGenera)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tion": {
          "$ref": "#/definitions/Id"
        },
        "requestedRecords": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "recordId": {
                "$ref": "#/definitions/Id"
              },
              "digest": {
                "$ref": "#/definitions/Digest"
              },
              "payload": {
                "$ref": "#/definitions/ArtifactRef"
              },
              "attachments": {
                "type": "array",
                "items": {
                  "$ref": "#/definitions/ArtifactRef"
                },
                "maxItems": 32,
                "uniqueItems": true
              }
            },
            "required": [
              "recordId",
              "digest",
              "payload",
              "attachments"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 32,
          "uniqueItems": true
        },
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "purpose": {
          "type": "string",
          "enum": [
            "context-recall",
            "projection-verification"
          ]
        },
        "maxBytes": {
          "type": "integer",
          "minimum": 1024,
          "maximum": 1048576
        },
        "remainingTtlMs": {
          "type": "integer",
          "minimum": 1,
          "maximum": 30000
        },
        "chunkBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "materialRequestId",
        "target",
        "sourceId",
        "sourceGeneration",
        "requestedRecords",
        "purpose",
        "maxBytes",
        "remainingTtlMs",
        "chunkBytes"
      ],
      "additionalProperties": false,
      "$comment": "Core-minted immutable request. Save exact target, source identity/generation, record digest, complete payload/attachment ArtifactRef witnesses, original deadline and maxByt)TANSR_SCHEMA",
    R"TANSR_SCHEMA(es before emission. Request bytes are the sum of distinct requested artifact bytes and must fit maxBytes and materialBytes (1 MiB hard cap); never request a partial arbitrary slice as if it were the committed original. Fixed upload slots are 65536 bytes, or a smaller positive negotiated materialChunkBytes. Replays do not renew the deadline. One pending->received CAS competes with timeout, revocation and binding close on this request, not unrelated binding revisions. No client prompt authority or host path. chunkBytes freezes this issued request's upload slot width from its effective Limits; each requested artifact must fit at most 16 slots. Later limits changes cannot resize issued slots. The host persists the actual original issuedAt/deadline, using the request's effective lifetime rather than a hard-coded 30-second deadline. remainingTtlMs is a decreasing transport observation, never a new lifetime on replay."
    },
    "MaterialResponseRequest": {
      "type": "object",
      "properties": {
        "pro)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tocol": {
          "const": "sdk2-ext-v1"
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Id"
        },
        "results": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "recordId": {
                "$ref": "#/definitions/Id"
              },
              "digest": {
                "$ref": "#/definitions/Digest"
              },
              "payload": {
                "$ref": "#/definitions/MaterialUploadRef"
              },
              "attachments": {
                "type": "array",
                "items": {
                  "$)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ref": "#/definitions/MaterialUploadRef"
                },
                "maxItems": 32,
                "uniqueItems": true
              }
            },
            "required": [
              "recordId",
              "digest",
              "payload",
              "attachments"
            ],
            "additionalProperties": false
          },
          "minItems": 1,
          "maxItems": 32
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "materialRequestId",
        "target",
        "sourceId",
        "sourceGeneration",
        "results"
      ],
      "additionalProperties": false,
      "$comment": "Only references to this request's committed uploads. Recheck auth, saved target/sourceGeneration/witnesses/deadline and pending->received CAS atomically with admission receipt. Every returned record must match its saved digest and all requested payload/attachment witnesses; distinct record IDs only. Unrelated archive ACK/binding revision )TANSR_SCHEMA",
    R"TANSR_SCHEMA(changes cannot reject a valid response. Same admitted operation replay returns its receipt without another transition; a competing different response after receipt is rejected. No self-reported role, trust, priority, permission, paths or original body. Follow the common operation epoch gate: authenticate current scope, look up the original operation key and compare semantic witness first; an already-admitted identical replay returns its original receipt without new effects even after epoch retirement. Unknown/expired/retired epoch cannot admit a new operation. Explicit operation-status reads remain available for retained receipts; a missing receipt never proves non-execution."
    },
    "MaterialReceipt": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        },
        "state": {
          "type": "string)TANSR_SCHEMA",
    R"TANSR_SCHEMA(",
          "enum": [
            "pending",
            "received",
            "verified",
            "core-consumed",
            "rejected"
          ]
        },
        "revision": {
          "allOf": [
            {
              "$ref": "#/definitions/Sequence"
            }
          ],
          "$comment": "Material-request-local monotone observation version, not binding revision and never a required response CAS input."
        },
        "acceptedRecordIds": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Id"
          },
          "minItems": 0,
          "maxItems": 32,
          "uniqueItems": true
        },
        "reason": {
          "type": "string",
          "enum": [
            "source_unavailable",
            "stale_generation",
            "invalid_coverage",
            "request_expired",
            "material_rejected",
            "forbidden",
            "binding_closed"
          ]
        }
      },
      "required": [
        "protoco)TANSR_SCHEMA",
    R"TANSR_SCHEMA(l",
        "bindingId",
        "materialRequestId",
        "state",
        "revision",
        "acceptedRecordIds"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "state": {
                "const": "rejected"
              }
            }
          },
          "then": {
            "required": [
              "reason"
            ]
          },
          "else": {
            "not": {
              "required": [
                "reason"
              ]
            }
          }
        },
        {
          "if": {
            "properties": {
              "state": {
                "const": "pending"
              }
            }
          },
          "then": {
            "properties": {
              "acceptedRecordIds": {
                "maxItems": 0
              },
              "revision": {
                "const": "0"
              }
            }
          }
        }
      ],
      "$comment": "pending is )TANSR_SCHEMA",
    R"TANSR_SCHEMA(an issued request, received is durable ingestion, verified is successful core validation, core-consumed is actual use. None means release: while core has a reference/assembly/read lease, artifacts stay pinned. Close/revoke prevent new core admission and preserve already-held bytes until core cancellation/drain releases all holds. Rejection is terminal and carries reason; timeout/revoke/close must win the same request CAS as response. On every NEW core read/assembly lease, atomically recheck current authorization, target/history/deletion/projection witnesses and source identity/generation against the saved request under the same exclusion boundary as lease acquisition. A received/verified receipt cannot authorize a new lease after restore/delete/revocation/source restart. Existing holders are cancelled/drained before release, never freed by changing generations."
    },
    "BindingCloseRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "expectedRevision",
        "generations"
      ],
      "additionalProperties": false,
      "$comment": "Atomically commit a closing intent and completed operation receipt, stop new capture/material admission and cancel pending material requests. Binding state remains closing until archive drain, accepted work and core-held artifacts fully settle; inspect BindingView, not pending operation receipt. Preserve old current history and source copies. Not deletion or rollback."
    },
    "MutationReceipt": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1)TANSR_SCHEMA",
    R"TANSR_SCHEMA("
        },
        "request": {
          "$ref": "#/definitions/RequestIdentity"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "operation": {
          "type": "string",
          "enum": [
            "binding-create",
            "archive-ack",
            "material-response",
            "binding-close"
          ]
        },
        "semanticDigest": {
          "$ref": "#/definitions/Digest"
        },
        "state": {
          "type": "string",
          "enum": [
            "accepted",
            "completed",
            "rejected"
          ]
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "outcomeRef": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "protocol",
        "request",
        "bindingId",
        "operation",
        "semanticDigest",
        "state",
        "revision",
        "outcomeRef"
      ],
      "additionalProperties": false,
      "$comment":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( "Persist admission+semantic witness before side effects. Recover uncertain admissions from durable receipt, never create another operation. accepted does not imply source durable or core consumed. Same request replay returns this same operation identity after authorization recheck. binding-close completed means its durable close intent was committed, not that all resource drain has finished. Final ACK also completes its operation independently of binding resource state. Terminal operation receipts are retained until both their terminal TTL and admission epoch expire; unresolved actual work has separate durable state and cannot be GCed merely because the close receipt expires. Current authorization precedes replay; retained same-key/same-semantics receipt lookup precedes the fresh epoch admission gate. Epoch retirement rejects only new admissions, not truthful recovery of already-admitted same-operation results.",
      "allOf": [
        {
          "if": {
            "properties": {
              "operatio)TANSR_SCHEMA",
    R"TANSR_SCHEMA(n": {
                "const": "binding-close"
              }
            }
          },
          "then": {
            "properties": {
              "state": {
                "enum": [
                  "completed",
                  "rejected"
                ]
              }
            }
          }
        }
      ]
    },
    "OperationStatusRequest": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "protocol": {
              "const": "sdk2-ext-v1"
            },
            "operation": {
              "const": "binding-create"
            },
            "sessionId": {
              "$ref": "#/definitions/LegacyId"
            },
            "request": {
              "$ref": "#/definitions/RequestIdentity"
            }
          },
          "required": [
            "protocol",
            "operation",
            "sessionId",
            "request"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "properties": {
            "protocol": {
              "const": "sdk2-ext-v1"
            },
            "operation": {
              "enum": [
                "archive-ack",
                "material-response",
                "binding-close"
              ]
            },
            "bindingId": {
              "$ref": "#/definitions/Id"
            },
            "request": {
              "$ref": "#/definitions/RequestIdentity"
            }
          },
          "required": [
            "protocol",
            "operation",
            "bindingId",
            "request"
          ],
          "additionalProperties": false
        }
      ],
      "$comment": "GET /v3/sdk2/operations selects binding-create by authenticated scope+original sessionId+operationEpoch+requestId even if response lost before bindingId was known. Other operations use bindingId plus exact operation+original identity. No scope from body/query. Retired epoch does not suppress admitted status; missing/cleared receipt mean)TANSR_SCHEMA",
    R"TANSR_SCHEMA(s receipt_expired, never not executed. Decode queries into this strict DTO before lookup."
    },
    "EventFrame": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "eventId": {
          "$ref": "#/definitions/Id"
        },
        "cursor": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "generations": {
          "$ref": "#/definitions/Generations"
        },
        "eventType": {
          "type": "string",
          "enum": [
            "archive.records-available",
            "archive.status",
            "material.request",
            "material.status",
            "binding.status"
          ]
        },
        "payload": {
          "type": "object"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "eventId",
        "cursor",
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "revision",
        "generations",
        "eventType",
        "payload"
      ],
      "additionalProperties": false,
      "$comment": "Only independent /v3/sdk2/bindings/:id/events, independent replay log/cursor; never emit through KernelEvent, legacy AgentStreamEvent or stdout. Unknown required event stops extension and cannot advance archive ACK.",
      "allOf": [
        {
          "if": {
            "properties": {
              "eventType": {
                "const": "archive.records-available"
              }
            }
          },
          "then": {
            "properties": {
              "payload": {
                "type": "object",
                "properties": {
                  "publishedThroughSequence": {
                    "$ref": "#/definitions/Sequence"
                  }
                },
                "required": [
                  "publishedThroughSequence"
                ],
                "additionalProperties": false
              }
            }
          }
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
        {
          "if": {
            "properties": {
              "eventType": {
                "const": "archive.status"
              }
            }
          },
          "then": {
            "properties": {
              "payload": {
                "$ref": "#/definitions/ArchiveStatus"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "eventType": {
                "const": "material.request"
              }
            }
          },
          "then": {
            "properties": {
              "payload": {
                "$ref": "#/definitions/MaterialRequest"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "eventType": {
                "const": "material.status"
              }
            }
          },
          "then": {
            "properties": {
              "payload": {
                "$ref": "#/definitions/MaterialReceipt"
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       }
            }
          }
        },
        {
          "if": {
            "properties": {
              "eventType": {
                "const": "binding.status"
              }
            }
          },
          "then": {
            "properties": {
              "payload": {
                "$ref": "#/definitions/BindingView"
              }
            }
          }
        }
      ]
    },
    "ErrorResponse": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "code": {
          "type": "string",
          "enum": [
            "invalid_request",
            "protocol_version_mismatch",
            "unauthorized",
            "forbidden",
            "unsupported_capability",
            "capability_unconfirmed",
            "binding_conflict",
            "stale_generation",
            "archive_gap",
            "source_unavailable",
            "inv)TANSR_SCHEMA",
    R"TANSR_SCHEMA(alid_coverage",
            "capacity_exceeded",
            "request_id_conflict",
            "request_expired",
            "receipt_expired",
            "payload_too_large",
            "material_rejected",
            "binding_closed",
            "internal_error",
            "epoch_unavailable",
            "stream_cursor_unknown",
            "stream_cursor_expired",
            "stream_gap"
          ]
        },
        "status": {
          "type": "integer",
          "enum": [
            400,
            401,
            403,
            409,
            410,
            413,
            422,
            429,
            500,
            503
          ]
        },
        "message": {
          "type": "string",
          "minLength": 1,
          "maxLength": 1024
        },
        "retryAction": {
          "type": "string",
          "enum": [
            "none",
            "same-request",
            "query-status",
            "rebind"
          ]
        },
        "retryAfterMs": {
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "type": "integer",
          "minimum": 1,
          "maximum": 60000
        }
      },
      "required": [
        "protocol",
        "requestId",
        "code",
        "status",
        "message",
        "retryAction"
      ],
      "additionalProperties": false,
      "$comment": "Independent envelope; never leak new code/enum onto legacy errors or SSE. Timeout/connection loss means outcome unconfirmed. query-status retains the original request identity. Messages contain no paths, credentials, raw materials or cross-principal resource details.",
      "allOf": [
        {
          "if": {
            "properties": {
              "code": {
                "const": "invalid_request"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 400
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "protocol_version_mismatch"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 400
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "unauthorized"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 401
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "properties": {
              "code": {
                "const": "forbidden"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 403
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "unsupported_capability"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 422
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capabilit)TANSR_SCHEMA",
    R"TANSR_SCHEMA(y_unconfirmed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 503
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "query-status"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "binding_conflict"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none",
                  "rebind"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stale_generation"
              }
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none",
                  "rebind"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "archive_gap"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "source_unavailable"
              }
            }
          },
          "then": {
            ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(properties": {
              "status": {
                "const": 503
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "same-request",
                  "query-status"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "invalid_coverage"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 422
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capacity_exceeded"
              }
            }
          },
          "then": {
            "properties": {
              "sta)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tus": {
                "const": 429
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "same-request",
                  "query-status"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "request_id_conflict"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "request_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( 410
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "receipt_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 410
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "payload_too_large"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 413
              },
              "retryAction": {
                "type":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "material_rejected"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 422
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "binding_closed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "none"
               )TANSR_SCHEMA",
    R"TANSR_SCHEMA( ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "internal_error"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 500
              },
              "retryAction": {
                "type": "string",
                "enum": [
                  "query-status"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "epoch_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 503
              },
              "retryAction": {
                "const": "same-request"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: {
                "const": "stream_cursor_unknown"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stream_cursor_expired"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 410
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stream_gap"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "const": 409
              },
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           "retryAction": {
                "const": "none"
              }
            }
          }
        }
      ]
    },
    "EpochView": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/Id"
        },
        "issuedAt": {
          "type": "string",
          "format": "date-time"
        },
        "expiresAt": {
          "type": "string",
          "format": "date-time"
        },
        "state": {
          "const": "active"
        }
      },
      "required": [
        "id",
        "issuedAt",
        "expiresAt",
        "state"
      ],
      "additionalProperties": false,
      "$comment": "Trusted host initialization/independent rotator durably prepares current epoch. GET reads snapshot and never mints/renews one. No current live epoch => epoch_unavailable for admission. Rotation disables old write admission, never hides already admitted operation status; retired/unknown IDs cannot be recreated from requests. S1 epoch lifetime defaults to 360)TANSR_SCHEMA",
    R"TANSR_SCHEMA(0000ms, hard maximum 86400000ms; trusted host validates 0<expiresAt-issuedAt<=effective epochLifetimeMs. Terminal receipt cannot be collected before BOTH epoch expiry and terminal retention expiry; unresolved operations remain. An active epoch is never a license to recreate a collected request ID."
    },
    "BindingTargetRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        }
      },
      "required": [
        "protocol",
        "sessionId"
      ],
      "additionalProperties": false
    },
    "BindingTargetView": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "bindingId": {
          "anyOf": [
            {
              "$ref": "#/definiti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ons/Id"
            },
            {
              "type": "null"
            }
          ]
        },
        "operationEpoch": {
          "anyOf": [
            {
              "$ref": "#/definitions/EpochView"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "target",
        "revision",
        "bindingId",
        "operationEpoch"
      ],
      "additionalProperties": false,
      "$comment": "GET /v3/sdk2/sessions/:sessionId/binding-target reads owned legacy session snapshot only; no archive/epoch creation. No extension state returns explicit host-observed unbound revision 0 and null bindingId. POST checks target generation AND sourceSnapshotDigest and extension-slot expectedRevision atomically. Snapshot source unavailable returns error, never synthetic empty history."
    },
    "RecordSequence": {
      "allOf": [
        {
          "$ref": "#/definitions/Sequence"
        },
        {
          "n)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ot": {
            "const": "0"
          }
        }
      ],
      "$comment": "Record ordinals start at 1. Zero is only an empty published/acknowledged watermark, never a record ordinal."
    },
    "MaterialUploadRef": {
      "type": "object",
      "properties": {
        "uploadId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "uploadId"
      ],
      "additionalProperties": false,
      "$comment": "Opaque server-issued ID. Resolve only a committed upload belonging to this authenticated scope, binding and still-valid materialRequestId, whose full bytes/SHA/ID exactly match the originally saved ArtifactRef witness. Never accept an arbitrary external ArtifactRef or client-chosen new body."
    },
    "MaterialUploadChunkRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#)TANSR_SCHEMA",
    R"TANSR_SCHEMA(/definitions/Id"
        },
        "target": {
          "$ref": "#/definitions/Target"
        },
        "sourceId": {
          "$ref": "#/definitions/Id"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Id"
        },
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "offset": {
          "type": "integer",
          "minimum": 0,
          "maximum": 1048576,
          "multipleOf": 1
        },
        "bytes": {
          "type": "integer",
          "minimum": 0,
          "maximum": 65536
        },
        "chunkSha256": {
          "$ref": "#/definitions/Digest"
        },
        "base64": {
          "type": "string",
          "maxLength": 87384,
          "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "materialRequestId",
        "target",
        "sourceId",
        "sourceGeneration",
        "artifactId",
        "offse)TANSR_SCHEMA",
    R"TANSR_SCHEMA(t",
        "bytes",
        "chunkSha256",
        "base64"
      ],
      "additionalProperties": false,
      "$comment": "POST /v3/sdk2/bindings/:bindingId/materials/:materialRequestId/uploads/:artifactId/chunks. Only a saved issued request authorizes upload; GET/POST never invent a missing request. Scope+binding+materialRequestId+artifactId+fixed slot offset is the idempotency key, not a new operation epoch. Slot width is the request's frozen effective materialChunkBytes (<=65536); offset=i*width; non-final chunks equal width, final equals remaining witnessed bytes. Empty artifact has exactly offset=0/bytes=0/empty base64 and empty SHA. Validate canonical base64, decoded byte count and chunk SHA before storing. Same slot/same bytes is idempotent and not recharged; conflicting bytes fail request_id_conflict. No overlap, gaps at commit, new bytes/witness or arbitrary offset. Whole distinct-upload bytes are reserved once per request within 1MiB and app/user/host quotas. Complete SHA over ordered original by)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tes must equal saved witness before state committed. TTL is original request TTL, no chunk/reset/replay renewal; late/unknown requests never reopen."
    },
    "MaterialUploadStatusRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        },
        "artifactId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "materialRequestId",
        "artifactId"
      ],
      "additionalProperties": false,
      "$comment": "GET the upload resource by originally issued materialRequestId+artifactId even when first chunk response was lost. Authenticated read only, no allocation, TTL renewal or upload restart. No upload yet is a source_unavailable error, never an empty committed result."
    },
    "MaterialUploadStatus": {
      "typ)TANSR_SCHEMA",
    R"TANSR_SCHEMA(e": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        },
        "uploadId": {
          "$ref": "#/definitions/Id"
        },
        "artifact": {
          "$ref": "#/definitions/ArtifactRef"
        },
        "state": {
          "enum": [
            "receiving",
            "committed"
          ]
        },
        "chunkBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536
        },
        "receivedOffsets": {
          "type": "array",
          "items": {
            "type": "integer",
            "minimum": 0,
            "maximum": 1048576
          },
          "minItems": 1,
          "maxItems": 16,
          "uniqueItems": true
        },
        "receivedBytes": {
          "type": "integer",
          "minimum": 0,
          "maximum": 1048576
        },
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "remainingTtlMs": {
          "type": "integer",
          "minimum": 1,
          "maximum": 30000
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "materialRequestId",
        "uploadId",
        "artifact",
        "state",
        "chunkBytes",
        "receivedOffsets",
        "receivedBytes",
        "remainingTtlMs"
      ],
      "additionalProperties": false,
      "$comment": "POST chunk and GET status response. At most 16 fixed slots in S1 at the frozen 65536-byte width (request issuance rejects any artifact needing more than 16 slots at its frozen width). Artifact is copied from saved server witness, never from client bytes. offsets sorted, aligned, unique; bytes equals actual distinct chunks. committed requires every slot, exact total bytes and whole SHA. State is upload receipt, not material received/core use. Entire response must remain under controlBytes."
    },
    "MaterialStatusRequest": {
      "type": "object",
      "properties": {
        "proto)TANSR_SCHEMA",
    R"TANSR_SCHEMA(col": {
          "const": "sdk2-ext-v1"
        },
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "materialRequestId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "protocol",
        "bindingId",
        "materialRequestId"
      ],
      "additionalProperties": false,
      "$comment": "GET /v3/sdk2/bindings/:bindingId/materials/:materialRequestId returns latest MaterialReceipt for this authenticated scope. Read only; no reissue or deadline refresh. Query after extension SSE loss does not resubmit material or change old stream."
    },
    "ArchiveAckFormat": {
      "type": "string",
      "const": "split-receipts-v1"
    },
    "ArchiveArtifactReceipt": {
      "type": "object",
      "properties": {
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        },
        "state": {
          "const": "durably-stored"
        }
      },
      "required": )TANSR_SCHEMA",
    R"TANSR_SCHEMA([
        "artifactId",
        "sha256",
        "state"
      ],
      "additionalProperties": false,
      "$comment": "Raw artifact SHA, never payloadDigest/recordDigest. Exact per-category first-occurrence sets are derived from every whole record in the requested coverage. No duplicate IDs, foreign objects or divergent witnesses."
    },
    "ClientPlatform": {
      "type": "object",
      "properties": {
        "platform": {
          "enum": [
            "windows",
            "linux",
            "macos",
            "android",
            "ios",
            "harmony"
          ]
        },
        "arch": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "language": {
          "type": "string",
          "minLength": 1,
          "maxLength": 64
        },
        "runtimeVersion": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "adapterVersion": {
          "type": "string",
          "minLen)TANSR_SCHEMA",
    R"TANSR_SCHEMA(gth": 1,
          "maxLength": 128
        }
      },
      "required": [
        "platform",
        "arch",
        "language",
        "runtimeVersion",
        "adapterVersion"
      ],
      "additionalProperties": false
    },
    "ExecutionTarget": {
      "type": "object",
      "properties": {
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "connectionRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "workspaceId": {
          "$ref": "#/definitions/Id"
        },
        "workspaceRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "interpreter": {
          "$ref": "#/definitions/ExecutionInterpreter"
        }
      },
      "required": [
        "executorId",
        "connectionId",
        "connectionRevision",
        "workspaceId",
        "workspaceRevision"
      ],
      "additionalProperties": false
    },
    "ExecutionBinding": {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "type": "object",
      "properties": {
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "target": {
          "$ref": "#/definitions/ExecutionTarget"
        }
      },
      "required": [
        "bindingId",
        "revision",
        "target"
      ],
      "additionalProperties": false
    },
    "EffectiveTool": {
      "type": "object",
      "properties": {
        "name": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "executionKind": {
          "enum": [
            "managed-service",
            "bound-device"
          ]
        },
        "available": {
          "type": "boolean"
        },
        "unavailableReason": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            {
              "type": "null"
            }
          ]
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   }
      },
      "required": [
        "name",
        "executionKind",
        "available",
        "unavailableReason"
      ],
      "additionalProperties": false
    },
    "SessionInitializeRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "platform": {
          "$ref": "#/definitions/ClientPlatform"
        },
        "requestedTools": {
          "type": "array",
          "items": {
            "type": "string",
            "minLength": 1,
            "maxLength": 128
          },
          "maxItems": 128
        }
      },
      "required": [
        "protocol",
        "sessionId",
        "platform"
      ],
      "additionalProperties": false
    },
    "SessionExecutionCapabilities": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "sessionId": {
          "$ref":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( "#/definitions/LegacyId"
        },
        "platform": {
          "anyOf": [
            {
              "$ref": "#/definitions/ClientPlatform"
            },
            {
              "type": "null"
            }
          ]
        },
        "capabilityRevision": {
          "$ref": "#/definitions/Digest"
        },
        "effectiveTools": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/EffectiveTool"
          },
          "maxItems": 128
        },
        "binding": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionBinding"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "sessionId",
        "platform",
        "capabilityRevision",
        "effectiveTools",
        "binding"
      ],
      "additionalProperties": false
    },
    "ExecutorRegistrationRequest": {
      "type": "object",
      "properties": {
        "protocol")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: {
          "const": "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "platform": {
          "$ref": "#/definitions/ClientPlatform"
        },
        "workspaces": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "workspaceId": {
                "$ref": "#/definitions/Id"
              },
              "revision": {
                "$ref": "#/definitions/Sequence"
              }
            },
            "required": [
              "workspaceId",
              "revision"
            ],
            "additionalProperties": false
          },
          "maxItems": 32
        },
        "operations": {
          "type": "array",
          "items": {
            "enum": [
              "fs.inspect",
              "fs.read",
              "fs.write",
              "fs.mkdir",
              "fs.list",
              "process.exec",
              "tool.invoke"
            ]
          })TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
          "maxItems": 7,
          "uniqueItems": true
        },
        "tools": {
          "type": "array",
          "maxItems": 64,
          "items": {
            "type": "object",
            "properties": {
              "name": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "definitionDigest": {
                "$ref": "#/definitions/Digest"
              }
            },
            "required": [
              "name",
              "definitionDigest"
            ],
            "additionalProperties": false
          }
        },
        "interpreter": {
          "$ref": "#/definitions/ExecutionInterpreter"
        }
      },
      "required": [
        "protocol",
        "executorId",
        "platform",
        "workspaces",
        "operations"
      ],
      "additionalProperties": false
    },
    "ExecutorConnection": {
      "type": "object",
      "properties": {
        "protocol": {
          "const":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "connectionRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "expiresAt": {
          "type": "string",
          "format": "date-time"
        },
        "heartbeatAfterMs": {
          "type": "integer",
          "minimum": 1000,
          "maximum": 30000
        }
      },
      "required": [
        "protocol",
        "executorId",
        "connectionId",
        "connectionRevision",
        "expiresAt",
        "heartbeatAfterMs"
      ],
      "additionalProperties": false
    },
    "ExecutorHeartbeatRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "protocol",
        "executorId",
        "connectionId"
      ],
      "additionalProperties": false
    },
    "ExecutionBindingRequest": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "workspaceId": {
          "$ref": "#/definitions/Id"
        },
        "expectedCapabilityRevision": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "protocol",
        "sessionId",
        "executorId",
        "connectionId",
        "workspaceId",
        "expectedCapabilityRevision"
      ],
      "additionalProperties": false
    },
    "ResourceRequest": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "operation": {
              "con)TANSR_SCHEMA",
    R"TANSR_SCHEMA(st": "fs.inspect"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "followLinks": {
                  "type": "boolean"
                }
              },
              "required": [
                "path",
                "followLinks"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.read"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "offset": {
                )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "length": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 65536
                }
              },
              "required": [
                "path",
                "offset",
                "length"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.write"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "expectedHash": {
                  "anyOf": [
            )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        {
                      "$ref": "#/definitions/Digest"
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "bytesBase64": {
                  "type": "string",
                  "maxLength": 174764,
                  "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
                }
              },
              "required": [
                "path",
                "expectedHash",
                "bytesBase64"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.mkdir"
            },
            "args": {
              "type": "object",
              "properties": {
                "path)TANSR_SCHEMA",
    R"TANSR_SCHEMA(": {
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "path"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.list"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "path"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "type": "object",
          "properties": {
            "operation": {
              "const": "process.exec"
            },
            "args": {
              "type": "object",
              "properties": {
                "command": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32768
                },
                "cwd": {
                  "type": "string",
                  "maxLength": 4096
                },
                "timeoutMs": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 120000
                },
                "maxOutputBytes": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 65536
                },
                "interpreter": {
                  "$ref": "#/definitions/ExecutionInterpreter"
                }
              },
              "required": [
                "command",
                "cwd",
                )TANSR_SCHEMA",
    R"TANSR_SCHEMA("timeoutMs",
                "maxOutputBytes",
                "interpreter"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "tool.invoke"
            },
            "args": {
              "type": "object",
              "properties": {
                "name": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "definitionDigest": {
                  "$ref": "#/definitions/Digest"
                },
                "argsJson": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32768
                }
              },
              "required": [
                "name",
                "def)TANSR_SCHEMA",
    R"TANSR_SCHEMA(initionDigest",
                "argsJson"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        }
      ]
    },
    "ResourceResult": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.inspect"
            },
            "args": {
              "type": "object",
              "properties": {
                "kind": {
                  "enum": [
                    "file",
                    "directory",
                    "symlink"
                  ]
                },
                "size": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "mtimeMs": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32,
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(                "pattern": "^(0|[1-9][0-9]*)(\\.[0-9]+)?$"
                },
                "realpath": {
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "kind",
                "size",
                "mtimeMs",
                "realpath"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.read"
            },
            "args": {
              "type": "object",
              "properties": {
                "bytesBase64": {
                  "type": "string",
                  "maxLength": 87384,
                  "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
                }
              })TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
              "required": [
                "bytesBase64"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.write"
            },
            "args": {
              "type": "object",
              "properties": {
                "hash": {
                  "$ref": "#/definitions/Digest"
                }
              },
              "required": [
                "hash"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.mkdir"
            },)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
            "args": {
              "type": "object",
              "properties": {},
              "required": [],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.list"
            },
            "args": {
              "type": "object",
              "properties": {
                "entries": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "properties": {
                      "name": {
                        "type": "string",
                        "minLength": 1,
                        "maxLength": 255
                      },
                      "kind": {
                        "enum": [
                          "file",
                          "directo)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ry",
                          "symlink"
                        ]
                      }
                    },
                    "required": [
                      "name",
                      "kind"
                    ],
                    "additionalProperties": false
                  },
                  "maxItems": 501
                }
              },
              "required": [
                "entries"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "process.exec"
            },
            "args": {
              "type": "object",
              "properties": {
                "stdout": {
                  "type": "string",
                  "maxLength": 65536
                },
                "stderr": {
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(              "type": "string",
                  "maxLength": 65536
                },
                "exitCode": {
                  "anyOf": [
                    {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 11,
                      "pattern": "^(0|[1-9][0-9]*|-[1-9][0-9]*)$"
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "signalName": {
                  "anyOf": [
                    {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 32
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "durationMs": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "time)TANSR_SCHEMA",
    R"TANSR_SCHEMA(dOut": {
                  "type": "boolean"
                },
                "aborted": {
                  "type": "boolean"
                }
              },
              "required": [
                "stdout",
                "stderr",
                "exitCode",
                "signalName",
                "durationMs",
                "timedOut",
                "aborted"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "tool.invoke"
            },
            "args": {
              "type": "object",
              "properties": {
                "resultJson": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32768
                }
              },
              "re)TANSR_SCHEMA",
    R"TANSR_SCHEMA(quired": [
                "resultJson"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        }
      ]
    },
    "ExecutionOperation": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "binding": {
          "$ref": "#/definitions/ExecutionBinding"
        },
        "toolName": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128
        },
        "request": {
          "$ref": "#/definitions/ResourceRequest"
        },
        "digest": {
          "$ref": "#/definitions/Digest"
        },
        "expiresAt": {
          "type")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: "string",
          "format": "date-time"
        }
      },
      "required": [
        "protocol",
        "operationId",
        "sessionId",
        "scope",
        "binding",
        "toolName",
        "request",
        "digest",
        "expiresAt"
      ],
      "additionalProperties": false
    },
    "ExecutionBatch": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "operations": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ExecutionOperation"
          },
          "maxItems": 16
        }
      },
      "required": [
        "protocol",
        "executorId",
        "connectionId",
        "operations"
      ],
      "additionalProperties": false
    },
    "ExecutionReceiptRequest": {
      "type": "object",
      "properties": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA("protocol": {
          "const": "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "digest": {
          "$ref": "#/definitions/Digest"
        },
        "status": {
          "enum": [
            "completed",
            "failed",
            "unknown"
          ]
        },
        "result": {
          "anyOf": [
            {
              "$ref": "#/definitions/ResourceResult"
            },
            {
              "type": "null"
            }
          ]
        },
        "errorCode": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "executorId",
        "connectionId",
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(        "operationId",
        "digest",
        "status",
        "result",
        "errorCode"
      ],
      "additionalProperties": false
    },
    "ExecutionStatus": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "operation": {
          "$ref": "#/definitions/ExecutionOperation"
        },
        "status": {
          "enum": [
            "pending",
            "completed",
            "failed",
            "unknown"
          ]
        },
        "receipt": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionReceiptRequest"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "operation",
        "status",
        "receipt"
      ],
      "additionalProperties": false
    },
    "ExecutionInterpreter": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/I)TANSR_SCHEMA",
    R"TANSR_SCHEMA(d"
        },
        "revision": {
          "$ref": "#/definitions/Id"
        },
        "hostShell": {
          "enum": [
            "posix",
            "powershell"
          ]
        }
      },
      "required": [
        "id",
        "revision",
        "hostShell"
      ],
      "additionalProperties": false
    },
    "ExecutionBoundaryApplication": {
      "type": "object",
      "properties": {
        "authorizationRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "policyTools": {
          "type": "array",
          "items": {
            "type": "string",
            "minLength": 1,
            "maxLength": 128
          },
          "maxItems": 128,
          "uniqueItems": true
        },
        "platformCapabilities": {
          "type": "array",
          "items": {
            "enum": [
              "imageGen",
              "videoGen",
              "webSearch",
              "speechToText",
              "textToSpeech"
            ]
          },
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(maxItems": 5,
          "uniqueItems": true
        },
        "executionProfile": {
          "enum": [
            "legacy",
            "bound-device-v1"
          ]
        }
      },
      "required": [
        "authorizationRevision",
        "policyTools",
        "platformCapabilities",
        "executionProfile"
      ],
      "additionalProperties": false
    },
    "ExecutionBoundaryExecutor": {
      "type": "object",
      "properties": {
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "connectionRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "platform": {
          "$ref": "#/definitions/ClientPlatform"
        },
        "operations": {
          "type": "array",
          "items": {
            "enum": [
              "fs.inspect",
              "fs.read",
              "fs.write",
              "fs.mkdir",
              "fs.list",
              "process.exec",
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(              "tool.invoke"
            ]
          },
          "maxItems": 7,
          "uniqueItems": true
        },
        "workspaces": {
          "type": "array",
          "items": {
            "type": "object",
            "properties": {
              "workspaceId": {
                "$ref": "#/definitions/Id"
              },
              "revision": {
                "$ref": "#/definitions/Sequence"
              }
            },
            "required": [
              "workspaceId",
              "revision"
            ],
            "additionalProperties": false
          },
          "maxItems": 32
        }
      },
      "required": [
        "executorId",
        "connectionId",
        "connectionRevision",
        "platform",
        "operations",
        "workspaces"
      ],
      "additionalProperties": false
    },
    "ExecutionBoundary": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "sessionId": {
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "$ref": "#/definitions/LegacyId"
        },
        "revision": {
          "$ref": "#/definitions/Digest"
        },
        "clientPlatform": {
          "anyOf": [
            {
              "$ref": "#/definitions/ClientPlatform"
            },
            {
              "type": "null"
            }
          ]
        },
        "application": {
          "$ref": "#/definitions/ExecutionBoundaryApplication"
        },
        "requestedTools": {
          "anyOf": [
            {
              "type": "array",
              "items": {
                "type": "string",
                "minLength": 1,
                "maxLength": 128
              },
              "maxItems": 128,
              "uniqueItems": true
            },
            {
              "type": "null"
            }
          ]
        },
        "executor": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionBoundaryExecutor"
            },
            {
              "type": "null"
            }
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     ]
        },
        "effectiveTools": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/EffectiveTool"
          },
          "maxItems": 128
        },
        "binding": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionBinding"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "sessionId",
        "revision",
        "clientPlatform",
        "application",
        "requestedTools",
        "executor",
        "effectiveTools",
        "binding"
      ],
      "additionalProperties": false
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_5[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/contracts/terminal-observation-v1/2026-09-27",
  "title": "终端只读资源与输出关联观察",
  "description": "独立加法合同；关闭请求仍沿原DELETE 202；资源观察不触发关闭或排空。未观测不等于已完成。",
  "definitions": {
    "OutputCorrelation": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/LegacyContract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "toolCallId": {
          "$ref": "#/definitions/LegacyId"
        },
        "operation": {
          "$ref": "#/definitions/OperationReference"
        },
        "outputAuthority": {
          "enum": [
            "session-events",
            "tool-output"
          ]
        }
      },
      "required": [
        "contract",
        "session",
        "toolCallId",
        "operation",
        "outputAuthority"
      ],
      "additionalProperties": false
    },
    "Contract": {
      "const": "terminal-observation-v)TANSR_SCHEMA",
    R"TANSR_SCHEMA(1"
    },
    "SessionReference": {
      "type": "object",
      "properties": {
        "sessionContract": {
          "$ref": "#/definitions/SessionContract"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        }
      },
      "required": [
        "sessionContract",
        "sessionId"
      ],
      "additionalProperties": false
    },
    "SessionContract": {
      "enum": [
        "sdk1",
        "sdk2-offload-v1"
      ]
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Existing opaque ID; no trimming, case folding or Unicode normalization. Do not mint authority from this string."
    },
    "OperationReference": {
      "type": "object",
      "properties": {
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "operationId",
        "requestDigest"
      ],
      "add)TANSR_SCHEMA",
    R"TANSR_SCHEMA(itionalProperties": false
    },
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "ErrorCode": {
      "enum": [
        "invalid_request",
        "protocol_version_mismatch",
        "unauthorized",
        "forbidden",
        "not_found",
        "unsupported_capability",
        "capability_unconfirmed",
        "source_unavailable",
        "stale_generation",
        "binding_conflict",
        "revision_conflict",
        "busy",
        "capacity_exceeded",
        "payload_too_large",
        "output_gap",
        "integrity_mismatch",
        "commit_unknown",
        "context_transition_required",
        "request_conflict",
        "request_limit"
      ]
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|)TANSR_SCHEMA",
    R"TANSR_SCHEMA(92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "ResourceState": {
      "enum": [
        "active",
        "accepted",
        "draining",
        "completed",
        "failed",
        "unknown"
      ]
    },
    "ResourceError": {
      "enum": [
        "close_failed",
        "publication_failed",
        "settlement_failed",
        "settlement_unavailable"
      ]
    },
    "ResourcesResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "epochStartSeq": {
          "$ref": "#/definitions/Sequence"
        },
        "state": {
          "$ref": "#/definitions/ResourceState"
        },
        "closeRequested": {
          "type": "boolean"
        },
        "executionEnded": {
          "type": "boolean"
        },
        "errorCode": {
          "anyOf": [
            {
              "$ref": "#/definitions/ResourceError"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "contract",
        "session",
        "epochStartSeq",
        "state",
        "closeRequested",
        "executionEnded",
        "errorCode"
      ],
      "additionalProperties": false
    },
    "CorrelationsResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "correlations": {
          "type": "array",
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "items": {
            "$ref": "#/definitions/OutputCorrelation"
          },
          "maxItems": 32
        },
        "truncated": {
          "type": "boolean"
        }
      },
      "required": [
        "contract",
        "session",
        "correlations",
        "truncated"
      ],
      "additionalProperties": false
    },
    "ErrorResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "code": {
          "$ref": "#/definitions/ErrorCode"
        },
        "status": {
          "enum": [
            400,
            401,
            403,
            404,
            409,
            413,
            429,
            503
          ]
        },
        "retryAction": {
          "enum": [
            "none",
            "discover",
            "refresh",
            "reconcile",
            "backoff"
          ]
        }
      },
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "required": [
        "contract",
        "requestId",
        "code",
        "status",
        "retryAction"
      ],
      "additionalProperties": false
    },
    "LegacyContract": {
      "const": "terminal-services-v1"
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_6[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/contracts/terminal-profile-v1.schema.json",
  "title": "Terminal profile v1",
  "description": "Independent read-only app_user catalog and own 1d usage. No price, money, numerical quota or budget.",
  "definitions": {
    "Contract": {
      "const": "terminal-profile-v1"
    },
    "Text": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512
    },
    "NullableText": {
      "anyOf": [
        {
          "$ref": "#/definitions/Text"
        },
        {
          "type": "null"
        }
      ]
    },
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "SessionContract": {
      "enum": [
        "sdk1",
        "sdk2-offload-v1"
      ]
    },
    "SessionReference": {
      "type": "object",
      "properties": {
        "sessionContract": {
          "$ref": "#/definitions/SessionContract"
        },
        "se)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ssionId": {
          "$ref": "#/definitions/Text"
        }
      },
      "required": [
        "sessionContract",
        "sessionId"
      ],
      "additionalProperties": false
    },
    "Model": {
      "type": "object",
      "properties": {
        "handle": {
          "$ref": "#/definitions/Text"
        },
        "displayName": {
          "$ref": "#/definitions/Text"
        },
        "manufacturer": {
          "$ref": "#/definitions/NullableText"
        },
        "family": {
          "$ref": "#/definitions/NullableText"
        }
      },
      "required": [
        "handle",
        "displayName",
        "manufacturer",
        "family"
      ],
      "additionalProperties": false
    },
    "Aliases": {
      "type": "object",
      "maxProperties": 1024,
      "propertyNames": {
        "$ref": "#/definitions/Text"
      },
      "additionalProperties": {
        "$ref": "#/definitions/Text"
      }
    },
    "Catalog": {
      "type": "object",
      "properties": {
        "models":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( {
          "type": "array",
          "maxItems": 1024,
          "items": {
            "$ref": "#/definitions/Model"
          }
        },
        "aliases": {
          "$ref": "#/definitions/Aliases"
        },
        "capabilities": {
          "$ref": "#/definitions/Capabilities"
        }
      },
      "required": [
        "models",
        "aliases",
        "capabilities"
      ],
      "additionalProperties": false
    },
    "Quota": {
      "type": "object",
      "properties": {
        "state": {
          "const": "not_exposed"
        },
        "enforcement": {
          "const": "gateway"
        }
      },
      "required": [
        "state",
        "enforcement"
      ],
      "additionalProperties": false
    },
    "Counter": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "Usage": {
      "type": "object",
      "properties": {
        "window": {
          "const": "1d"
        },
        "endUserId": {
          "$ref": "#/definitions)TANSR_SCHEMA",
    R"TANSR_SCHEMA(/Text"
        },
        "requests": {
          "$ref": "#/definitions/Counter"
        },
        "inTokens": {
          "$ref": "#/definitions/Counter"
        },
        "outTokens": {
          "$ref": "#/definitions/Counter"
        },
        "cacheRTokens": {
          "$ref": "#/definitions/Counter"
        },
        "cacheWTokens": {
          "$ref": "#/definitions/Counter"
        }
      },
      "required": [
        "window",
        "endUserId",
        "requests",
        "inTokens",
        "outTokens",
        "cacheRTokens",
        "cacheWTokens"
      ],
      "additionalProperties": false
    },
    "CatalogResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "models": {
          "type": "array",
          "maxItems": 1024,
          "items": {
            "$ref": "#/definitions/Model"
          }
        },
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "aliases": {
          "$ref": "#/definitions/Aliases"
        },
        "quota": {
          "$ref": "#/definitions/Quota"
        },
        "capabilities": {
          "$ref": "#/definitions/Capabilities"
        }
      },
      "required": [
        "contract",
        "session",
        "models",
        "aliases",
        "quota",
        "capabilities"
      ],
      "additionalProperties": false
    },
    "UsageResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "usage": {
          "$ref": "#/definitions/Usage"
        },
        "quota": {
          "$ref": "#/definitions/Quota"
        }
      },
      "required": [
        "contract",
        "session",
        "usage",
        "quota"
      ],
      "additionalProperties": false
    },
    "ErrorCode": {
      "enum": [
        "invalid_request",
        "protocol_vers)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ion_mismatch",
        "unauthorized",
        "forbidden",
        "not_found",
        "unsupported_capability",
        "capability_unconfirmed",
        "source_unavailable",
        "stale_generation",
        "binding_conflict",
        "revision_conflict",
        "busy",
        "capacity_exceeded",
        "payload_too_large",
        "output_gap",
        "integrity_mismatch",
        "commit_unknown",
        "context_transition_required",
        "request_conflict",
        "request_limit"
      ]
    },
    "ErrorResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "code": {
          "$ref": "#/definitions/ErrorCode"
        },
        "status": {
          "enum": [
            400,
            401,
            403,
            404,
            409,
            413,
            429,
            503
          ]
        },
        "retryA)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ction": {
          "enum": [
            "none",
            "discover",
            "refresh",
            "reconcile",
            "backoff"
          ]
        }
      },
      "required": [
        "contract",
        "requestId",
        "code",
        "status",
        "retryAction"
      ],
      "additionalProperties": false
    },
    "CapabilityTools": {
      "type": "object",
      "properties": {
        "read": {
          "type": "boolean"
        },
        "write": {
          "type": "boolean"
        },
        "edit": {
          "type": "boolean"
        },
        "glob": {
          "type": "boolean"
        },
        "grep": {
          "type": "boolean"
        },
        "list": {
          "type": "boolean"
        },
        "shell": {
          "type": "boolean"
        },
        "process": {
          "type": "boolean"
        },
        "webFetch": {
          "type": "boolean"
        },
        "webSearch": {
          "type": "boolean"
        },
        "http": {
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   "type": "boolean"
        },
        "todoWrite": {
          "type": "boolean"
        },
        "askUser": {
          "type": "boolean"
        },
        "agent": {
          "type": "boolean"
        },
        "skills": {
          "type": "boolean"
        },
        "mcp": {
          "type": "boolean"
        },
        "customTools": {
          "type": "boolean"
        }
      },
      "required": [
        "read",
        "write",
        "edit",
        "glob",
        "grep",
        "list",
        "shell",
        "process",
        "webFetch",
        "webSearch",
        "http",
        "todoWrite",
        "askUser",
        "agent",
        "skills",
        "mcp",
        "customTools"
      ],
      "additionalProperties": false
    },
    "CapabilityPlatform": {
      "type": "object",
      "properties": {
        "imageGen": {
          "type": "boolean"
        },
        "videoGen": {
          "type": "boolean"
        },
        "webSearch": {
          "type": "boolean"
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
        "speechToText": {
          "type": "boolean"
        },
        "textToSpeech": {
          "type": "boolean"
        }
      },
      "required": [
        "imageGen",
        "videoGen",
        "webSearch",
        "speechToText",
        "textToSpeech"
      ],
      "additionalProperties": false
    },
    "BoundDeviceTools": {
      "type": "object",
      "properties": {
        "read": {
          "type": "boolean"
        },
        "write": {
          "type": "boolean"
        },
        "edit": {
          "type": "boolean"
        },
        "glob": {
          "type": "boolean"
        },
        "grep": {
          "type": "boolean"
        },
        "list": {
          "type": "boolean"
        },
        "shell": {
          "type": "boolean"
        },
        "process": {
          "type": "boolean"
        },
        "skills": {
          "type": "boolean"
        },
        "mcp": {
          "type": "boolean"
        },
        "customTools": {
          "type": "boolean)TANSR_SCHEMA",
    R"TANSR_SCHEMA("
        }
      },
      "required": [],
      "additionalProperties": false
    },
    "ExecutionCapabilities": {
      "type": "object",
      "properties": {
        "version": {
          "const": "bound-device-v1"
        },
        "boundDevice": {
          "type": "object",
          "properties": {
            "tools": {
              "$ref": "#/definitions/BoundDeviceTools"
            }
          },
          "required": [
            "tools"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "version",
        "boundDevice"
      ],
      "additionalProperties": false
    },
    "Capabilities": {
      "type": "object",
      "properties": {
        "tools": {
          "$ref": "#/definitions/CapabilityTools"
        },
        "platform": {
          "$ref": "#/definitions/CapabilityPlatform"
        },
        "execution": {
          "$ref": "#/definitions/ExecutionCapabilities"
        }
      },
      "required": [
        "tools",
        "p)TANSR_SCHEMA",
    R"TANSR_SCHEMA(latform"
      ],
      "additionalProperties": false
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_7[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/contracts/terminal-services-v1/2026-09-26.candidate-7",
  "title": "RFC-SERVE-NET-1 终端服务合同候选（尚未冻结）",
  "description": "终端能力候选：发现、受限执行状态、工具输出、typed后台tool.invoke profile、记忆管理与会话配置。只有真实安装和授权才可协商；旧SDK2引用定义保持同值。",
  "definitions": {
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "LegacyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "Existing opaque ID; no trimming, case folding or Unicode normalization. Do not mint authority from this string."
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[)TANSR_SCHEMA",
    R"TANSR_SCHEMA(0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$"
    },
    "Scope": {
      "type": "object",
      "properties": {
        "applicationScopeId": {
          "$ref": "#/definitions/Id"
        },
        "endUserId": {
          "$ref": "#/definitions/LegacyId"
        },
        "authorizationRevision": {
          "$ref": "#/definitions/Sequence"
        }
      },
      "required": [
        "applicationScopeId",
        "endUserId",
        "authorizationRevision"
      ],
      "additionalProperties": false,
      "$comment": "Response/host-adapter only. Client requests have no Scope field. Reauthenticate and authorize every operation; archive roles never imply approve or execute."
    },
    "ExecutionBindin)TANSR_SCHEMA",
    R"TANSR_SCHEMA(g": {
      "type": "object",
      "properties": {
        "bindingId": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "target": {
          "$ref": "#/definitions/ExecutionTarget"
        }
      },
      "required": [
        "bindingId",
        "revision",
        "target"
      ],
      "additionalProperties": false
    },
    "ExecutionTarget": {
      "type": "object",
      "properties": {
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "connectionRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "workspaceId": {
          "$ref": "#/definitions/Id"
        },
        "workspaceRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "interpreter": {
          "$ref": "#/definitions/ExecutionInterpreter"
        }
      },
      "required": [
        "executorId",
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "connectionId",
        "connectionRevision",
        "workspaceId",
        "workspaceRevision"
      ],
      "additionalProperties": false
    },
    "ExecutionInterpreter": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Id"
        },
        "hostShell": {
          "enum": [
            "posix",
            "powershell"
          ]
        }
      },
      "required": [
        "id",
        "revision",
        "hostShell"
      ],
      "additionalProperties": false
    },
    "Contract": {
      "const": "terminal-services-v1"
    },
    "SchemaRevision": {
      "const": "2026-09-26.candidate-7"
    },
    "SessionContract": {
      "enum": [
        "sdk1",
        "sdk2-offload-v1"
      ]
    },
    "Feature": {
      "enum": [
        "execution-stream-v1",
        "execution-background-v1",
        "memory-lifecycle-v1",
        "session-configuration-v1"
      ]
    },
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "SessionReference": {
      "type": "object",
      "properties": {
        "sessionContract": {
          "$ref": "#/definitions/SessionContract"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        }
      },
      "required": [
        "sessionContract",
        "sessionId"
      ],
      "additionalProperties": false
    },
    "FeatureAvailability": {
      "type": "object",
      "properties": {
        "feature": {
          "$ref": "#/definitions/Feature"
        },
        "supported": {
          "type": "boolean"
        },
        "installed": {
          "type": "boolean"
        },
        "authorization": {
          "enum": [
            "allowed",
            "denied",
            "unconfirmed"
          ]
        },
        "device": {
          "enum": [
            "available",
            "unavailable",
            "not-required",
            "unconfirmed"
          ]
        }
      },
      "required": [
        "feature",
        "supported",
        "i)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nstalled",
        "authorization",
        "device"
      ],
      "additionalProperties": false
    },
    "Limits": {
      "type": "object",
      "properties": {
        "maxControlBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 262144
        },
        "maxBlockBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 16384
        },
        "maxBatchBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 65536
        },
        "maxPendingBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 2097152
        },
        "maxRetainedBytes": {
          "type": "integer",
          "minimum": 1,
          "maximum": 8388608
        }
      },
      "required": [
        "maxControlBytes",
        "maxBlockBytes",
        "maxBatchBytes",
        "maxPendingBytes",
        "maxRetainedBytes"
      ],
      "additionalProperties": false
    },
    "CapabilitiesResponse": {
      "type": )TANSR_SCHEMA",
    R"TANSR_SCHEMA("object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "schemaRevision": {
          "$ref": "#/definitions/SchemaRevision"
        },
        "schemaSha256": {
          "$ref": "#/definitions/Digest"
        },
        "sessionContracts": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/SessionContract"
          },
          "minItems": 1,
          "maxItems": 2,
          "uniqueItems": true
        },
        "features": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/FeatureAvailability"
          },
          "minItems": 4,
          "maxItems": 4
        },
        "limits": {
          "$ref": "#/definitions/Limits"
        }
      },
      "required": [
        "contract",
        "schemaRevision",
        "schemaSha256",
        "sessionContracts",
        "features",
        "limits"
      ],
      "additionalProperties": false
    },
    "BindingRequest": {
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA("type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "executionBinding": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionBinding"
            },
            {
              "type": "null"
            }
          ]
        },
        "required": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Feature"
          },
          "minItems": 0,
          "maxItems": 4,
          "uniqueItems": true
        },
        "optional": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Feature"
          },
          "minItems": 0,
          "maxItems": 4,
          "uniqueItems": true
        }
      },
      "required": [
        "contract",
        "requestId",
        "session",
        "execut)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ionBinding",
        "required",
        "optional"
      ],
      "additionalProperties": false
    },
    "BindingResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "executionBinding": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionBinding"
            },
            {
              "type": "null"
            }
          ]
        },
        "accepted": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Feature"
          },
          "minItems": 0,
          "maxItems": 4,
          "uniqueItems": true
        },
        "unavailable": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/FeatureA)TANSR_SCHEMA",
    R"TANSR_SCHEMA(vailability"
          },
          "minItems": 0,
          "maxItems": 4
        },
        "outputAuthority": {
          "enum": [
            "session-events",
            "tool-output"
          ]
        },
        "limits": {
          "$ref": "#/definitions/Limits"
        }
      },
      "required": [
        "contract",
        "requestId",
        "session",
        "scope",
        "executionBinding",
        "accepted",
        "unavailable",
        "outputAuthority",
        "limits"
      ],
      "additionalProperties": false
    },
    "OperationReference": {
      "type": "object",
      "properties": {
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "operationId",
        "requestDigest"
      ],
      "additionalProperties": false
    },
    "OutputBlock": {
      "type": "object",
      "properties": {
        "seq": {
          "$ref": "#/definiti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ons/Sequence"
        },
        "byteOffset": {
          "$ref": "#/definitions/Sequence"
        },
        "channel": {
          "enum": [
            "stdout",
            "stderr"
          ]
        },
        "encoding": {
          "enum": [
            "utf-8",
            "binary"
          ]
        },
        "byteLength": {
          "type": "integer",
          "minimum": 1,
          "maximum": 16384
        },
        "payloadDigest": {
          "$ref": "#/definitions/Digest"
        },
        "base64": {
          "type": "string",
          "minLength": 4,
          "maxLength": 21848,
          "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
        }
      },
      "required": [
        "seq",
        "byteOffset",
        "channel",
        "encoding",
        "byteLength",
        "payloadDigest",
        "base64"
      ],
      "additionalProperties": false
    },
    "OutputSeal": {
      "type": "object",
      "properties": {
        "lastSeq": {
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "totalBytes": {
          "$ref": "#/definitions/Sequence"
        },
        "payloadDigest": {
          "$ref": "#/definitions/Digest"
        },
        "truncated": {
          "type": "boolean"
        }
      },
      "required": [
        "lastSeq",
        "totalBytes",
        "payloadDigest",
        "truncated"
      ],
      "additionalProperties": false
    },
    "OutputBatchRequest": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "operation": {
          "$ref": "#/definitions/OperationReference"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
        "blocks": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/OutputBlock"
          },
          "minItems": 0,
          "maxItems": 32
        },
        "seal": {
          "anyOf": [
            {
              "$ref": "#/definitions/OutputSeal"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "contract",
        "session",
        "operation",
        "executorId",
        "connectionId",
        "blocks",
        "seal"
      ],
      "additionalProperties": false
    },
    "OutputStatus": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "operation": {
          "$ref": "#/definitions/OperationReference"
        },
        "state": {
          "enum": [
            "available",
            "receiving",
            "complete",
            "truncated",
            "gap",
            "unavailable"
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       ]
        },
        "acceptedThrough": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "durableThrough": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "retainedFrom": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "nextByteOffset": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "seal": {
          "anyOf": [
            {
              "$ref": "#/definitions/OutputSeal"
            },
            {
              "type": "null"
            }
          ])TANSR_SCHEMA",
    R"TANSR_SCHEMA(
        }
      },
      "required": [
        "contract",
        "operation",
        "state",
        "acceptedThrough",
        "durableThrough",
        "retainedFrom",
        "nextByteOffset",
        "seal"
      ],
      "additionalProperties": false
    },
    "OutputEvent": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "type": {
              "const": "output.block"
            },
            "operation": {
              "$ref": "#/definitions/OperationReference"
            },
            "block": {
              "$ref": "#/definitions/OutputBlock"
            }
          },
          "required": [
            "contract",
            "type",
            "operation",
            "block"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/)TANSR_SCHEMA",
    R"TANSR_SCHEMA(definitions/Contract"
            },
            "type": {
              "const": "output.status"
            },
            "status": {
              "$ref": "#/definitions/OutputStatus"
            }
          },
          "required": [
            "contract",
            "type",
            "status"
          ],
          "additionalProperties": false
        }
      ]
    },
    "ExecutorEvent": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "eventId": {
          "$ref": "#/definitions/Sequence"
        },
        "type": {
          "enum": [
            "operations-available",
            "operation-cancelled",
            "reconcile-required"
          ]
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "operation": {
          "anyOf": [
            {
              "$ref": "#/definitions/OperationReferen)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ce"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "contract",
        "eventId",
        "type",
        "executorId",
        "connectionId",
        "operation"
      ],
      "additionalProperties": false
    },
    "OutputCorrelation": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "toolCallId": {
          "$ref": "#/definitions/LegacyId"
        },
        "operation": {
          "$ref": "#/definitions/OperationReference"
        },
        "outputAuthority": {
          "enum": [
            "session-events",
            "tool-output"
          ]
        }
      },
      "required": [
        "contract",
        "session",
        "toolCallId",
        "operation",
        "outputAuthority"
      ],
      "additionalProperties": false
    },
    "ErrorC)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ode": {
      "enum": [
        "invalid_request",
        "protocol_version_mismatch",
        "unauthorized",
        "forbidden",
        "not_found",
        "unsupported_capability",
        "capability_unconfirmed",
        "source_unavailable",
        "stale_generation",
        "binding_conflict",
        "revision_conflict",
        "busy",
        "capacity_exceeded",
        "payload_too_large",
        "output_gap",
        "integrity_mismatch",
        "commit_unknown",
        "context_transition_required",
        "request_conflict",
        "request_limit"
      ]
    },
    "ErrorResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "code": {
          "$ref": "#/definitions/ErrorCode"
        },
        "status": {
          "enum": [
            400,
            401,
            403,
            404,
            409,
            413)TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
            429,
            503
          ]
        },
        "retryAction": {
          "enum": [
            "none",
            "discover",
            "refresh",
            "reconcile",
            "backoff"
          ]
        }
      },
      "required": [
        "contract",
        "requestId",
        "code",
        "status",
        "retryAction"
      ],
      "additionalProperties": false
    },
    "ConfigurationRevision": {
      "type": "integer",
      "minimum": 0,
      "maximum": 9007199254740991
    },
    "Thinking": {
      "type": "object",
      "properties": {
        "budget": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        }
      },
      "required": [],
      "additionalProperties": false
    },
    "SessionConfiguration": {
      "type": "object",
      "properties": {
        "revision": {
          "$ref": "#/definitions/ConfigurationRevision"
        },
        "model": {
          "anyOf": [
            {
            )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "type": "string",
              "minLength": 1,
              "maxLength": 512
            },
            {
              "type": "null"
            }
          ]
        },
        "thinking": {
          "anyOf": [
            {
              "$ref": "#/definitions/Thinking"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "revision",
        "model",
        "thinking"
      ],
      "additionalProperties": false
    },
    "ConfigurationChanges": {
      "type": "object",
      "properties": {
        "model": {
          "type": "string",
          "minLength": 1,
          "maxLength": 512
        },
        "thinking": {
          "anyOf": [
            {
              "$ref": "#/definitions/Thinking"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [],
      "additionalProperties": false
    },
    "ConfigurationRequest": {
      "type": "object",
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "expectedRevision": {
          "$ref": "#/definitions/ConfigurationRevision"
        },
        "changes": {
          "$ref": "#/definitions/ConfigurationChanges"
        }
      },
      "required": [
        "contract",
        "requestId",
        "session",
        "expectedRevision",
        "changes"
      ],
      "additionalProperties": false
    },
    "ConfigurationResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "configuration": {
          "$ref": "#/definitions/SessionConfiguration"
        }
      },
      "required": [
        "contract",
        "session",
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "configuration"
      ],
      "additionalProperties": false
    },
    "ConfigurationCommitResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "requestId": {
          "$ref": "#/definitions/Id"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "status": {
          "enum": [
            "changed",
            "replayed"
          ]
        },
        "configuration": {
          "$ref": "#/definitions/SessionConfiguration"
        }
      },
      "required": [
        "contract",
        "requestId",
        "session",
        "status",
        "configuration"
      ],
      "additionalProperties": false
    },
    "ExecutionStatus": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "operation": {
          "$ref": "#/definitions/ExecutionOperation"
        },
        "status": {
          "en)TANSR_SCHEMA",
    R"TANSR_SCHEMA(um": [
            "pending",
            "completed",
            "failed",
            "unknown"
          ]
        },
        "receipt": {
          "anyOf": [
            {
              "$ref": "#/definitions/ExecutionReceiptRequest"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "operation",
        "status",
        "receipt"
      ],
      "additionalProperties": false
    },
    "ExecutionOperation": {
      "type": "object",
      "properties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "sessionId": {
          "$ref": "#/definitions/LegacyId"
        },
        "scope": {
          "$ref": "#/definitions/Scope"
        },
        "binding": {
          "$ref": "#/definitions/ExecutionBinding"
        },
        "toolName": {
          "type": "string",
          "minLength": 1,
          "ma)TANSR_SCHEMA",
    R"TANSR_SCHEMA(xLength": 128
        },
        "request": {
          "$ref": "#/definitions/ResourceRequest"
        },
        "digest": {
          "$ref": "#/definitions/Digest"
        },
        "expiresAt": {
          "type": "string",
          "format": "date-time"
        }
      },
      "required": [
        "protocol",
        "operationId",
        "sessionId",
        "scope",
        "binding",
        "toolName",
        "request",
        "digest",
        "expiresAt"
      ],
      "additionalProperties": false
    },
    "ResourceRequest": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.inspect"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "followLinks": {
                  "type": "boolean"
                }
              },
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "required": [
                "path",
                "followLinks"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.read"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "offset": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "length": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 65536
                }
              },
              "required": [
                "path",
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "offset",
                "length"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.write"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                },
                "expectedHash": {
                  "anyOf": [
                    {
                      "$ref": "#/definitions/Digest"
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "bytesBase64": {
                  "type": "string",
                  "maxLength": 174764,
                  "pattern": )TANSR_SCHEMA",
    R"TANSR_SCHEMA("^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
                }
              },
              "required": [
                "path",
                "expectedHash",
                "bytesBase64"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.mkdir"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "path"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.list"
            },
            "args": {
              "type": "object",
              "properties": {
                "path": {
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "path"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "process.exec"
            },
            "args": {
              "type": "object",
              "properties": {
                "command": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength":)TANSR_SCHEMA",
    R"TANSR_SCHEMA( 32768
                },
                "cwd": {
                  "type": "string",
                  "maxLength": 4096
                },
                "timeoutMs": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 120000
                },
                "maxOutputBytes": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 65536
                },
                "interpreter": {
                  "$ref": "#/definitions/ExecutionInterpreter"
                }
              },
              "required": [
                "command",
                "cwd",
                "timeoutMs",
                "maxOutputBytes",
                "interpreter"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(  "properties": {
            "operation": {
              "const": "tool.invoke"
            },
            "args": {
              "type": "object",
              "properties": {
                "name": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 128
                },
                "definitionDigest": {
                  "$ref": "#/definitions/Digest"
                },
                "argsJson": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32768
                }
              },
              "required": [
                "name",
                "definitionDigest",
                "argsJson"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        }
      ]
    },
    "ExecutionReceiptRequest": {
      "type": "object",
      "pr)TANSR_SCHEMA",
    R"TANSR_SCHEMA(operties": {
        "protocol": {
          "const": "sdk2-ext-v1"
        },
        "executorId": {
          "$ref": "#/definitions/Id"
        },
        "connectionId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "digest": {
          "$ref": "#/definitions/Digest"
        },
        "status": {
          "enum": [
            "completed",
            "failed",
            "unknown"
          ]
        },
        "result": {
          "anyOf": [
            {
              "$ref": "#/definitions/ResourceResult"
            },
            {
              "type": "null"
            }
          ]
        },
        "errorCode": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 128
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "protocol",
        "executorId",
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "connectionId",
        "operationId",
        "digest",
        "status",
        "result",
        "errorCode"
      ],
      "additionalProperties": false
    },
    "ResourceResult": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.inspect"
            },
            "args": {
              "type": "object",
              "properties": {
                "kind": {
                  "enum": [
                    "file",
                    "directory",
                    "symlink"
                  ]
                },
                "size": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "mtimeMs": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32,
                  "pattern": "^(0|[1-9][0-9]*)(\\.[0-9]+)?$"
                },
                "realpath": {)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
                  "type": "string",
                  "maxLength": 4096
                }
              },
              "required": [
                "kind",
                "size",
                "mtimeMs",
                "realpath"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.read"
            },
            "args": {
              "type": "object",
              "properties": {
                "bytesBase64": {
                  "type": "string",
                  "maxLength": 87384,
                  "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
                }
              },
              "required": [
                "bytesBase64"
              ],
              "additionalPrope)TANSR_SCHEMA",
    R"TANSR_SCHEMA(rties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.write"
            },
            "args": {
              "type": "object",
              "properties": {
                "hash": {
                  "$ref": "#/definitions/Digest"
                }
              },
              "required": [
                "hash"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.mkdir"
            },
            "args": {
              "type": "object",
              "properties": {},
              "requi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(red": [],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "fs.list"
            },
            "args": {
              "type": "object",
              "properties": {
                "entries": {
                  "type": "array",
                  "items": {
                    "type": "object",
                    "properties": {
                      "name": {
                        "type": "string",
                        "minLength": 1,
                        "maxLength": 255
                      },
                      "kind": {
                        "enum": [
                          "file",
                          "directory",
                          "symlink"
                        ]
                      }
                )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
                    "required": [
                      "name",
                      "kind"
                    ],
                    "additionalProperties": false
                  },
                  "maxItems": 501
                }
              },
              "required": [
                "entries"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "process.exec"
            },
            "args": {
              "type": "object",
              "properties": {
                "stdout": {
                  "type": "string",
                  "maxLength": 65536
                },
                "stderr": {
                  "type": "string",
                  "maxLength": 65536
                },
                "ex)TANSR_SCHEMA",
    R"TANSR_SCHEMA(itCode": {
                  "anyOf": [
                    {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 11,
                      "pattern": "^(0|[1-9][0-9]*|-[1-9][0-9]*)$"
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "signalName": {
                  "anyOf": [
                    {
                      "type": "string",
                      "minLength": 1,
                      "maxLength": 32
                    },
                    {
                      "type": "null"
                    }
                  ]
                },
                "durationMs": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 9007199254740991
                },
                "timedOut": {
                  "type": "boolean"
                },
                "aborted": {
              )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "type": "boolean"
                }
              },
              "required": [
                "stdout",
                "stderr",
                "exitCode",
                "signalName",
                "durationMs",
                "timedOut",
                "aborted"
              ],
              "additionalProperties": false
            }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "operation": {
              "const": "tool.invoke"
            },
            "args": {
              "type": "object",
              "properties": {
                "resultJson": {
                  "type": "string",
                  "minLength": 1,
                  "maxLength": 32768
                }
              },
              "required": [
                "resultJson"
              ],
              "additionalProperties": false
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      }
          },
          "required": [
            "operation",
            "args"
          ],
          "additionalProperties": false
        }
      ]
    },
    "ExecutionState": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "execution": {
          "$ref": "#/definitions/ExecutionStatus"
        }
      },
      "required": [
        "contract",
        "session",
        "execution"
      ],
      "additionalProperties": false
    },
    "BackgroundIdentity": {
      "type": "object",
      "properties": {
        "taskId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        },
        "runtimeInstanceId": {
          "$ref": "#/definitions/Id"
        },
        "processInstanceId": {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "taskId",
        "operationId",
        "requestDigest",
        "runtimeInstanceId",
        "processInstanceId"
      ],
      "additionalProperties": false
    },
    "ArtifactReference": {
      "type": "object",
      "properties": {
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "artifactId",
        "operationId",
        "requestDigest"
      ],
      "additionalProperties": false
    },
    "BackgroundRequest": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "launch"
            },
            "command": {
              "type": "string",
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "minLength": 1,
              "maxLength": 8192
            },
            "cwd": {
              "type": "string",
              "maxLength": 4096
            },
            "mode": {
              "enum": [
                "background",
                "foreground-handoff"
              ]
            },
            "timeoutMs": {
              "type": "integer",
              "minimum": 1,
              "maximum": 86400000
            },
            "foregroundTimeoutMs": {
              "type": "integer",
              "minimum": 1,
              "maximum": 120000
            }
          },
          "required": [
            "contract",
            "action",
            "command",
            "cwd",
            "mode",
            "timeoutMs"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "co)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nst": "query"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            }
          },
          "required": [
            "contract",
            "action",
            "identity"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "cancel"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            }
          },
          "required": [
            "contract",
            "action",
            "identity"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "outputRead"
            },
           )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            },
            "offset": {
              "$ref": "#/definitions/Sequence"
            },
            "length": {
              "type": "integer",
              "minimum": 1,
              "maximum": 16384
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "offset",
            "length"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "artifactDelete"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/defini)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tions/ArtifactReference"
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact"
          ],
          "additionalProperties": false
        }
      ]
    },
    "BackgroundTaskState": {
      "type": "object",
      "properties": {
        "identity": {
          "$ref": "#/definitions/BackgroundIdentity"
        },
        "artifact": {
          "$ref": "#/definitions/ArtifactReference"
        },
        "state": {
          "enum": [
            "running",
            "completed",
            "failed",
            "cancelled",
            "unknown"
          ]
        },
        "exitCode": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": -2147483648,
              "maximum": 2147483647
            },
            {
              "type": "null"
            }
          ]
        },
        "signalName": {
          "anyOf": [
            {
              "type": "string",
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "minLength": 1,
              "maxLength": 64
            },
            {
              "type": "null"
            }
          ]
        },
        "totalBytes": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "truncated": {
          "type": "boolean"
        }
      },
      "required": [
        "identity",
        "artifact",
        "state",
        "exitCode",
        "signalName",
        "totalBytes",
        "truncated"
      ],
      "additionalProperties": false
    },
    "BackgroundResponse": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "launch"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "r)TANSR_SCHEMA",
    R"TANSR_SCHEMA(equired": [
            "contract",
            "action",
            "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "query"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "required": [
            "contract",
            "action",
            "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "cancel"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "required": [
            "contract",
            "action",
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "outputRead"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            },
            "offset": {
              "$ref": "#/definitions/Sequence"
            },
            "base64": {
              "type": "string",
              "maxLength": 21848,
              "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
            },
            "byteLength": {
              "type": "integer",
              "minimum": 0,
              "maximum": 16384
            },
            "payloadDigest": {
              "$ref": "#/definitions/Digest"
            },
           )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "nextOffset": {
              "$ref": "#/definitions/Sequence"
            },
            "complete": {
              "type": "boolean"
            },
            "gap": {
              "type": "boolean"
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "offset",
            "base64",
            "byteLength",
            "payloadDigest",
            "nextOffset",
            "complete",
            "gap"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "artifactDelete"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            },
            "stat)TANSR_SCHEMA",
    R"TANSR_SCHEMA(us": {
              "enum": [
                "deleted",
                "already_deleted"
              ]
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "status"
          ],
          "additionalProperties": false
        }
      ]
    },
    "MemoryIdentity": {
      "type": "object",
      "properties": {
        "kind": {
          "enum": [
            "server-managed",
            "client-managed",
            "developer-managed"
          ]
        },
        "domain": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "applicationScopeId": {
          "$ref": "#/definitions/Id"
        },
        "endUserId": {
          "$ref": "#/definitions/LegacyId"
        }
      },
      "required": [
        "kind")TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
        "domain",
        "sourceId",
        "sourceGeneration",
        "applicationScopeId",
        "endUserId"
      ],
      "additionalProperties": false
    },
    "MemoryCommand": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "pin"
            },
            "text": {
              "type": "string",
              "minLength": 1,
              "maxLength": 4096
            }
          },
          "required": [
            "kind",
            "text"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "remember"
            }
          },
          "required": [
            "kind"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "kind": {
              "const": "forget"
            },
            "topic": {
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(          "$ref": "#/definitions/MemoryIdentifier"
            }
          },
          "required": [
            "kind",
            "topic"
          ],
          "additionalProperties": false
        }
      ]
    },
    "MemoryCommandRequest": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "requestId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "operationId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "command": {
          "$ref": "#/definitions/MemoryCommand"
        }
      },
      "required": [
        "contract",
        "session")TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
        "requestId",
        "operationId",
        "sourceId",
        "sourceGeneration",
        "expectedRevision",
        "command"
      ],
      "additionalProperties": false
    },
    "MemoryReceipt": {
      "type": "object",
      "properties": {
        "requestId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "operationId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceId": {
          "$ref": "#/definitions/MemoryIdentifier"
        },
        "sourceGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "expectedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "status": {
          "enum": [
            "pending",
            "unknown",
            "committed",
            "failed"
          ]
        },
        "publishedRevision": {
          "$ref": "#/definitions/Sequence"
        },
        "errorCode": {
          "enum": [
            "operation_failed",
            "commit_unkn)TANSR_SCHEMA",
    R"TANSR_SCHEMA(own"
          ]
        },
        "executionComplete": {
          "type": "boolean"
        },
        "durable": {
          "type": "boolean"
        },
        "consumed": {
          "type": "boolean"
        }
      },
      "required": [
        "requestId",
        "operationId",
        "sourceId",
        "sourceGeneration",
        "expectedRevision",
        "status",
        "executionComplete",
        "durable",
        "consumed"
      ],
      "additionalProperties": false
    },
    "MemoryState": {
      "type": "object",
      "properties": {
        "identity": {
          "$ref": "#/definitions/MemoryIdentity"
        },
        "revision": {
          "$ref": "#/definitions/Sequence"
        },
        "deletionGeneration": {
          "$ref": "#/definitions/Sequence"
        },
        "available": {
          "type": "boolean"
        }
      },
      "required": [
        "identity",
        "revision",
        "deletionGeneration",
        "available"
      ],
      "additionalPro)TANSR_SCHEMA",
    R"TANSR_SCHEMA(perties": false
    },
    "MemoryStateResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "memory": {
          "$ref": "#/definitions/MemoryState"
        }
      },
      "required": [
        "contract",
        "session",
        "memory"
      ],
      "additionalProperties": false
    },
    "MemoryReceiptResponse": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "session": {
          "$ref": "#/definitions/SessionReference"
        },
        "receipt": {
          "anyOf": [
            {
              "$ref": "#/definitions/MemoryReceipt"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "contract",
        "session",
        "receipt"
      ],
      "additionalProp)TANSR_SCHEMA",
    R"TANSR_SCHEMA(erties": false
    },
    "MemoryIdentifier": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "MemoryPublicationEtag": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      "pattern": "^[^\\u0000-\\u001f\\u007f]+$"
    },
    "MemoryPublicationHead": {
      "type": "object",
      "properties": {
        "etag": {
          "$ref": "#/definitions/MemoryPublicationEtag"
        },
        "byteLength": {
          "type": "integer",
          "minimum": 1,
          "maximum": 4194304
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "etag",
        "byteLength",
        "sha256"
      ],
      "additionalProperties": false
    },
    "MemoryPublicationTransfer": {
      "type": "object",
      "properties": {
        "transferId": {
          "$ref": "#/definitions/Id"
        },
        "status": {
          "enum": [
            "sta)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ging",
            "committed",
            "conflict",
            "unknown"
          ]
        },
        "receivedBytes": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0,
              "maximum": 4194304
            },
            {
              "type": "null"
            }
          ]
        },
        "etag": {
          "anyOf": [
            {
              "$ref": "#/definitions/MemoryPublicationEtag"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "transferId",
        "status",
        "receivedBytes",
        "etag"
      ],
      "additionalProperties": false
    },
    "MemoryPublicationRequest": {
      "anyOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "head"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "read"
            },
            "etag": {
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "$ref": "#/definitions/MemoryPublicationEtag"
            },
            "offset": {
              "type": "integer",
              "minimum": 0,
              "maximum": 4194304
            },
            "length": {
              "type": "integer",
              "minimum": 1,
              "maximum": 12288
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "etag",
            "offset",
            "length"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definiti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ons/MemoryIdentifier"
            },
            "action": {
              "const": "begin"
            },
            "transferId": {
              "$ref": "#/definitions/Id"
            },
            "expectedEtag": {
              "anyOf": [
                {
                  "$ref": "#/definitions/MemoryPublicationEtag"
                },
                {
                  "type": "null"
                }
              ]
            },
            "byteLength": {
              "type": "integer",
              "minimum": 1,
              "maximum": 4194304
            },
            "sha256": {
              "$ref": "#/definitions/Digest"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transferId",
            "expectedEtag",
            "byteLength",
            "sha256"
          ],
          "additionalProperties": false
        },
        {
          "type": )TANSR_SCHEMA",
    R"TANSR_SCHEMA("object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "chunk"
            },
            "transferId": {
              "$ref": "#/definitions/Id"
            },
            "offset": {
              "type": "integer",
              "minimum": 0,
              "maximum": 4194304
            },
            "base64": {
              "type": "string",
              "maxLength": 16384,
              "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
            },
            "byteLength": {
              "type": "integer",
              "minimum": 1,
              "maximum": 12288
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     },
            "payloadDigest": {
              "$ref": "#/definitions/Digest"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transferId",
            "offset",
            "base64",
            "byteLength",
            "payloadDigest"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "commit"
            },
            "transferId": {
              "$ref": "#/definitio)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ns/Id"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transferId"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "query"
            },
            "transferId": {
              "$ref": "#/definitions/Id"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "a)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ction",
            "transferId"
          ],
          "additionalProperties": false
        }
      ],
      "description": "可信记忆域专用传输 profile，沿原 tool.invoke 日记及认证绑定，不进入模型工具表；最多4MiB、分块12KiB，禁止映射为任意终端路径。"
    },
    "MemoryPublicationResponse": {
      "anyOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "head"
            },
            "publication": {
              "anyOf": [
                {
                  "$ref": "#/definitions/MemoryPublicationHead"
                },
                {
                  "type": "null"
                }
              ]
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "publication"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "read"
            },
            "etag": {
              "$ref": "#/definitions/MemoryPublicationEtag"
            },
            "offset": {
              "type": "integer",
              "minimum": 0,
              "maximum": 4194304
            },
            "base64": {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             "type": "string",
              "maxLength": 16384,
              "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
            },
            "byteLength": {
              "type": "integer",
              "minimum": 0,
              "maximum": 12288
            },
            "payloadDigest": {
              "$ref": "#/definitions/Digest"
            },
            "nextOffset": {
              "type": "integer",
              "minimum": 0,
              "maximum": 4194304
            },
            "complete": {
              "type": "boolean"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "etag",
            "offset",
            "base64",
            "byteLength",
            "payloadDigest",
            "nextOffset",
            "complete"
          ],
          "additionalProperties": false
        },
        {
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "begin"
            },
            "transfer": {
              "$ref": "#/definitions/MemoryPublicationTransfer"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transfer"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "chunk"
            },
            "transfer": {
              "$ref": "#/definitions/MemoryPublicationTransfer"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transfer"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "commit"
            },
            "transfer": {
              "$ref": "#/definitions/MemoryPublicationTransfer"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transfer"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "sourceId": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "sourceGeneration": {
              "$ref": "#/definitions/Sequence"
            },
            "domainKey": {
              "$ref": "#/definitions/MemoryIdentifier"
            },
            "action": {
              "const": "query)TANSR_SCHEMA",
    R"TANSR_SCHEMA("
            },
            "transfer": {
              "$ref": "#/definitions/MemoryPublicationTransfer"
            }
          },
          "required": [
            "contract",
            "sourceId",
            "sourceGeneration",
            "domainKey",
            "action",
            "transfer"
          ],
          "additionalProperties": false
        }
      ],
      "description": "传输体只承担 publication CAS；不在终端重写记忆算法。CAS和committed回执须同一耐久事务。unknown不得自动重建原transfer。"
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_8[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/contracts/terminal-shell-sandbox-v1.schema.json",
  "title": "Terminal shell sandbox opt-in profile v1",
  "$comment": "SRV-02 独立能力合同。Background 定义来自未修改的 candidate-7 schema SHA256 8cd8c7c55a84c5700373aed75d5653a0737d718bfe0546641be367bda1a11896；call/批准关联及null响应的跨字段语义由双方codec验证。 前台raw捕获帽65536字节；ExecResult仅提供合计4096字节UTF8预览，output两通道摘要基于原严格解码且保留BOM的完整UTF8文本，成功文本字节数等于原raw字节数。原窗口缺失/缺块不能用预览冒充完整结果或重新执行。",
  "definitions": {
    "BackgroundRequest": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "launch"
            },
            "command": {
              "type": "string",
              "minLength": 1,
              "maxLength": 8192
            },
            "cwd": {
              "type": "string",
              "maxLength": 4096
            },
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "mode": {
              "enum": [
                "background",
                "foreground-handoff"
              ]
            },
            "timeoutMs": {
              "type": "integer",
              "minimum": 1,
              "maximum": 86400000
            },
            "foregroundTimeoutMs": {
              "type": "integer",
              "minimum": 1,
              "maximum": 120000
            }
          },
          "required": [
            "contract",
            "action",
            "command",
            "cwd",
            "mode",
            "timeoutMs"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "query"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            }
          },
          "required": [
      )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "contract",
            "action",
            "identity"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "cancel"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            }
          },
          "required": [
            "contract",
            "action",
            "identity"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "outputRead"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions)TANSR_SCHEMA",
    R"TANSR_SCHEMA(/ArtifactReference"
            },
            "offset": {
              "$ref": "#/definitions/Sequence"
            },
            "length": {
              "type": "integer",
              "minimum": 1,
              "maximum": 16384
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "offset",
            "length"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "artifactDelete"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            }
          },
          "required": [
            "contract",
            "action",
            "iden)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tity",
            "artifact"
          ],
          "additionalProperties": false
        }
      ]
    },
    "BackgroundContract": {
      "const": "terminal-services-v1"
    },
    "BackgroundIdentity": {
      "type": "object",
      "properties": {
        "taskId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        },
        "runtimeInstanceId": {
          "$ref": "#/definitions/Id"
        },
        "processInstanceId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "taskId",
        "operationId",
        "requestDigest",
        "runtimeInstanceId",
        "processInstanceId"
      ],
      "additionalProperties": false
    },
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$"
    },
    "Digest": {
      "type": "string",
      "pat)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tern": "^[a-f0-9]{64}$"
    },
    "ArtifactReference": {
      "type": "object",
      "properties": {
        "artifactId": {
          "$ref": "#/definitions/Id"
        },
        "operationId": {
          "$ref": "#/definitions/Id"
        },
        "requestDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "artifactId",
        "operationId",
        "requestDigest"
      ],
      "additionalProperties": false
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical un)TANSR_SCHEMA",
    R"TANSR_SCHEMA(signed decimal 0..2^63-1. Never parse through IEEE754 Number."
    },
    "BackgroundResponse": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "launch"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "required": [
            "contract",
            "action",
            "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "query"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "required": [
            "contract",
            "action",
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "cancel"
            },
            "task": {
              "$ref": "#/definitions/BackgroundTaskState"
            }
          },
          "required": [
            "contract",
            "action",
            "task"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "outputRead"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            },
            "offset": {)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
              "$ref": "#/definitions/Sequence"
            },
            "base64": {
              "type": "string",
              "maxLength": 21848,
              "pattern": "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$"
            },
            "byteLength": {
              "type": "integer",
              "minimum": 0,
              "maximum": 16384
            },
            "payloadDigest": {
              "$ref": "#/definitions/Digest"
            },
            "nextOffset": {
              "$ref": "#/definitions/Sequence"
            },
            "complete": {
              "type": "boolean"
            },
            "gap": {
              "type": "boolean"
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "offset",
            "base64",
            "byteLength",
            "payloadDigest",
            "nextOffset",
            "complete",
            "gap"
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/BackgroundContract"
            },
            "action": {
              "const": "artifactDelete"
            },
            "identity": {
              "$ref": "#/definitions/BackgroundIdentity"
            },
            "artifact": {
              "$ref": "#/definitions/ArtifactReference"
            },
            "status": {
              "enum": [
                "deleted",
                "already_deleted"
              ]
            }
          },
          "required": [
            "contract",
            "action",
            "identity",
            "artifact",
            "status"
          ],
          "additionalProperties": false
        }
      ]
    },
    "BackgroundTaskState": {
      "type": "object",
      "properties": {
        "identity": {
          "$ref": "#/definitions/BackgroundIdentity"
        },
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "artifact": {
          "$ref": "#/definitions/ArtifactReference"
        },
        "state": {
          "enum": [
            "running",
            "completed",
            "failed",
            "cancelled",
            "unknown"
          ]
        },
        "exitCode": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": -2147483648,
              "maximum": 2147483647
            },
            {
              "type": "null"
            }
          ]
        },
        "signalName": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 64
            },
            {
              "type": "null"
            }
          ]
        },
        "totalBytes": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ]
        },
        "truncated": {
          "type": "boolean"
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA( }
      },
      "required": [
        "identity",
        "artifact",
        "state",
        "exitCode",
        "signalName",
        "totalBytes",
        "truncated"
      ],
      "additionalProperties": false
    },
    "ExecutionInterpreter": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/Id"
        },
        "revision": {
          "$ref": "#/definitions/Id"
        },
        "hostShell": {
          "enum": [
            "posix",
            "powershell"
          ]
        }
      },
      "required": [
        "id",
        "revision",
        "hostShell"
      ],
      "additionalProperties": false
    },
    "Contract": {
      "const": "terminal-shell-sandbox-v1"
    },
    "Call": {
      "type": "object",
      "properties": {
        "turnId": {
          "$ref": "#/definitions/Id"
        },
        "toolCallId": {
          "$ref": "#/definitions/Id"
        }
      },
      "required": [
        "turnId",
        "toolCallId"
      ],)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
      "additionalProperties": false
    },
    "ApprovedCall": {
      "type": "object",
      "properties": {
        "turnId": {
          "$ref": "#/definitions/Id"
        },
        "toolCallId": {
          "$ref": "#/definitions/Id"
        },
        "argsDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "turnId",
        "toolCallId",
        "argsDigest"
      ],
      "additionalProperties": false
    },
    "Escalation": {
      "type": "object",
      "properties": {
        "previousDeniedOperationId": {
          "$ref": "#/definitions/Id"
        },
        "approvedCall": {
          "$ref": "#/definitions/ApprovedCall"
        }
      },
      "required": [
        "previousDeniedOperationId",
        "approvedCall"
      ],
      "additionalProperties": false
    },
    "Sandbox": {
      "type": "object",
      "properties": {
        "mode": {
          "enum": [
            "off",
            "on",
            "required"
          ]
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   },
        "capability": {
          "enum": [
            "none",
            "partial",
            "full"
          ]
        },
        "isolationDenied": {
          "type": "boolean"
        },
        "escalated": {
          "type": "boolean"
        }
      },
      "required": [
        "mode",
        "capability",
        "isolationDenied",
        "escalated"
      ],
      "additionalProperties": false
    },
    "ExecResult": {
      "type": "object",
      "properties": {
        "stdout": {
          "type": "string",
          "maxLength": 65536
        },
        "stderr": {
          "type": "string",
          "maxLength": 65536
        },
        "exitCode": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 11,
              "pattern": "^(0|[1-9][0-9]*|-[1-9][0-9]*)$"
            },
            {
              "type": "null"
            }
          ]
        },
        "signalName": {
          "anyOf": [
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        {
              "type": "string",
              "minLength": 1,
              "maxLength": 32
            },
            {
              "type": "null"
            }
          ]
        },
        "durationMs": {
          "type": "integer",
          "minimum": 0,
          "maximum": 9007199254740991
        },
        "timedOut": {
          "type": "boolean"
        },
        "aborted": {
          "type": "boolean"
        }
      },
      "required": [
        "stdout",
        "stderr",
        "exitCode",
        "signalName",
        "durationMs",
        "timedOut",
        "aborted"
      ],
      "additionalProperties": false
    },
    "Request": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "exec"
            },
            "call": {
              "$ref": "#/definitions/Call"
            },
            "command": {
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "type": "string",
              "minLength": 1,
              "maxLength": 32768
            },
            "cwd": {
              "type": "string",
              "maxLength": 4096
            },
            "timeoutMs": {
              "type": "integer",
              "minimum": 1,
              "maximum": 120000
            },
            "maxOutputBytes": {
              "type": "integer",
              "minimum": 1,
              "maximum": 65536
            },
            "interpreter": {
              "$ref": "#/definitions/ExecutionInterpreter"
            },
            "escalation": {
              "$ref": "#/definitions/Escalation"
            }
          },
          "required": [
            "contract",
            "action",
            "call",
            "command",
            "cwd",
            "timeoutMs",
            "maxOutputBytes",
            "interpreter"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "propertie)TANSR_SCHEMA",
    R"TANSR_SCHEMA(s": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "background"
            },
            "request": {
              "$ref": "#/definitions/BackgroundRequest"
            },
            "call": {
              "$ref": "#/definitions/Call"
            },
            "escalation": {
              "$ref": "#/definitions/Escalation"
            }
          },
          "required": [
            "contract",
            "action",
            "request"
          ],
          "additionalProperties": false
        }
      ]
    },
    "Response": {
      "oneOf": [
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "exec"
            },
            "result": {
              "anyOf": [
                {
                  "$ref": "#/definitions/ExecResult"
                },
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      {
                  "type": "null"
                }
              ]
            },
            "sandbox": {
              "$ref": "#/definitions/Sandbox"
            },
            "output": {
              "anyOf": [
                {
                  "$ref": "#/definitions/Output"
                },
                {
                  "type": "null"
                }
              ]
            }
          },
          "required": [
            "contract",
            "action",
            "result",
            "sandbox",
            "output"
          ],
          "additionalProperties": false
        },
        {
          "type": "object",
          "properties": {
            "contract": {
              "$ref": "#/definitions/Contract"
            },
            "action": {
              "const": "background"
            },
            "result": {
              "anyOf": [
                {
                  "$ref": "#/definitions/BackgroundResponse"
                },
                {
         )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "type": "null"
                }
              ]
            },
            "sandbox": {
              "$ref": "#/definitions/Sandbox"
            }
          },
          "required": [
            "contract",
            "action",
            "result",
            "sandbox"
          ],
          "additionalProperties": false
        }
      ]
    },
    "OutputChannel": {
      "type": "object",
      "properties": {
        "totalBytes": {
          "type": "integer",
          "minimum": 0,
          "maximum": 65536
        },
        "payloadDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "totalBytes",
        "payloadDigest"
      ],
      "additionalProperties": false
    },
    "Output": {
      "type": "object",
      "properties": {
        "previewTruncated": {
          "type": "boolean"
        },
        "stdout": {
          "$ref": "#/definitions/OutputChannel"
        },
        "stderr": {
          "$ref": "#/definitions/OutputCha)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nnel"
        }
      },
      "required": [
        "previewTruncated",
        "stdout",
        "stderr"
      ],
      "additionalProperties": false
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr std::string_view schema_chunks_9[]{
    R"TANSR_SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://tansr.com/contracts/unified-v1/2026-10-01.draft",
  "title": "Tansr Unified API Contract unified-v1 — 门面自有形、统一信封与统一头(schema 草案 drafted;依据 RFC-UAPI-1 v1.0-rc 会签稿与方案 §0 D7)",
  "description": "定义 unified-v1 的公共层:统一 manifest(GET /api/manifest)、部署级能力视图(GET /api/capabilities)、会话级能力闭合围栏(GET /api/sessions/:id/capabilities)、门面自有错误信封、阶段三域错误统一信封、统一事件包络与请求 / 响应头词表。事实源:doc/rfc/RFC-UAPI-1-统一信封与canonical单模块.md(§1 信封与头、§1.4 围栏、§1.5 事件包络、§2 错误矩阵、§3 字段消歧)与开发方案 §3.2 / §3.5 / §3.6。不改任何现有合同族的字段与 wire 字节;领域 payload 由各族子合同约束。HTTP 处理器按具名定义校验,不按根 anyOf。",
  "$comment": "与既有合同族同律:全部对象 additionalProperties:false(RequestHeaders / ResponseHeaders 校验的是统一头词表的投影);Id / Sequence / Digest 逐字复制自 terminal-services-v1。围栏 operations 采显式属性 + required(77 条恒全覆盖、无未知操作名,便于代码生成出固定键类型),不用 propertyNames 枚举。身份字段(applicationScopeId / endUserId / authorizationRevision)恒由服务端派生,任何请求形不含身份字段(RFC-SDK2-1 §9.1;方案 §2.3)。",
  "anyOf": [
    {
      "$ref": "#/definitions/Manifest"
    },
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA( {
      "$ref": "#/definitions/Capabilities"
    },
    {
      "$ref": "#/definitions/CapabilityClosure"
    },
    {
      "$ref": "#/definitions/FacadeError"
    },
    {
      "$ref": "#/definitions/UnifiedError"
    },
    {
      "$ref": "#/definitions/EventEnvelope"
    },
    {
      "$ref": "#/definitions/RequestHeaders"
    },
    {
      "$ref": "#/definitions/ResponseHeaders"
    }
  ],
  "definitions": {
    "Id": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[A-Za-z0-9][A-Za-z0-9._~-]*$",
      "description": "复制自 terminal-services-v1 / sdk2-ext-v1 的 Id。"
    },
    "Sequence": {
      "type": "string",
      "pattern": "^(0|[1-9][0-9]{0,17}|[1-8][0-9]{18}|9[0-1][0-9]{17}|92[0-1][0-9]{16}|922[0-2][0-9]{15}|9223[0-2][0-9]{14}|92233[0-6][0-9]{13}|922337[0-1][0-9]{12}|92233720[0-2][0-9]{10}|922337203[0-5][0-9]{9}|9223372036[0-7][0-9]{8}|92233720368[0-4][0-9]{7}|922337203685[0-3][0-9]{6}|9223372036854[0-6][0-9]{5}|92233720368547[0-6][0-9]{4}|922337203685)TANSR_SCHEMA",
    R"TANSR_SCHEMA(477[0-4][0-9]{3}|9223372036854775[0-7][0-9]{2}|922337203685477580[0-6]|9223372036854775807)$",
      "maxLength": 19,
      "description": "Canonical unsigned decimal 0..2^63-1(复制自 terminal-services-v1)。Never parse through IEEE754 Number。"
    },
    "Digest": {
      "type": "string",
      "pattern": "^[a-f0-9]{64}$",
      "description": "sha256 小写 hex 64 位(复制自 terminal-services-v1)。"
    },
    "Contract": {
      "const": "unified-v1",
      "description": "统一合同修订标识(RFC §1.1 tansr-contract;方案 §3.7 contractRevision)。"
    },
    "Domain": {
      "type": "string",
      "enum": [
        "discovery",
        "session",
        "execution",
        "archive",
        "archive-sync",
        "cache",
        "terminal",
        "terminal-observation",
        "terminal-profile"
      ],
      "description": "受理域词表 = 响应头 tansr-domain(RFC §1.1);discovery 为门面自有。与 route-table.ts ApiDomain 同集。"
    },
    "ClosureDomain": {
      "type": "string",
      "enum": [
        "session",
        "execution",
        ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(archive",
        "archive-sync",
        "cache",
        "terminal",
        "terminal-observation",
        "terminal-profile"
      ],
      "description": "围栏 / 部署级能力视图的域词表 = Domain 去 discovery(closure.ts CLOSURE_DOMAINS)。"
    },
    "FamilyId": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z0-9]+(-[a-z0-9]+)*$",
      "description": "合同族 id(manifest families[].id,如 agent-session-v1 / sdk2-ext-v1 / unified-v1)。"
    },
    "FamilyStatus": {
      "type": "string",
      "enum": [
        "frozen",
        "final",
        "additive",
        "candidate",
        "drafted"
      ],
      "description": "合同族状态(generate-manifest.mjs FAMILIES.status 词表)。"
    },
    "SessionFamily": {
      "type": "string",
      "enum": [
        "sdk1",
        "sdk2-offload-v1"
      ],
      "description": "会话族(请求头 tansr-session-family 取值;RFC §1.2;方案 §3.3 / D6 第 9 项按 B)。"
    },
    "RepoPath": {
      "type": "string",
      "minLength": 1,
      "maxLength": 256,
      ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(pattern": "^[^\\s\\\\]+$",
      "description": "仓内相对路径(正斜杠,无空白);只作文档 / 登记,不是可导航 URL。"
    },
    "SchemaHash": {
      "type": "string",
      "pattern": "^(sha256:[a-f0-9]{64}|none)$",
      "description": "受理域合同族源 schema 指纹:sha256:<hex> 或 none(族未登记)(RFC §1.1 tansr-schema-hash)。"
    },
    "Sha256Prefixed": {
      "type": "string",
      "pattern": "^sha256:[a-f0-9]{64}$",
      "description": "恒有值的指纹形(discovery 聚合 schemaHash)。"
    },
    "TraceId": {
      "type": "string",
      "pattern": "^[A-Za-z0-9._-]{1,128}$",
      "description": "观测关联 id = 响应头 x-request-id(采纳合法入站 x-request-id / traceparent trace-id / 自铸 UUID;observability/request-context.ts REQUEST_ID_PATTERN)。与业务 requestId 分离(RFC §1.3 / §3)。"
    },
    "IdempotencyKey": {
      "type": "string",
      "pattern": "^[\\x21-\\x7E]{1,128}$",
      "description": "客户端幂等键(Idempotency-Key 头 / 各域体内 request.requestId;RFC §1.2:ASCII 1–128)。"
    },
    "KeyPath": {
      "type": "array",
      "items": {
        "type": "string",
        "pattern": "^[)TANSR_SCHEMA",
    R"TANSR_SCHEMA(A-Za-z][A-Za-z0-9]*$",
        "maxLength": 64
      },
      "minItems": 1,
      "maxItems": 8,
      "description": "JSON 体内键路径(自根起逐级对象键;revision 7:ManifestFamily.requestIdPath / ManifestOperation.etagPath / expectedRevision.path)。"
    },
    "UnifiedCode": {
      "type": "string",
      "enum": [
        "invalid_request",
        "protocol_mismatch",
        "unauthorized",
        "forbidden",
        "not_found",
        "method_not_allowed",
        "gone",
        "conflict",
        "stale_generation",
        "gap",
        "capability_unavailable",
        "capacity_exceeded",
        "payload_too_large",
        "upstream_unavailable",
        "result_unknown",
        "rejected",
        "internal_error",
        "precondition_failed",
        "not_canonical"
      ],
      "description": "统一错误码 19 = 17 四族映射码(事实源 packages/server/contract/api-error-map.json unifiedCodes,顺序同表)+ precondition_failed(412,门面 / If-Match 前置)+ not_canonical(400,严格 canonical 入口拒收)(方案 §0 D7;RFC-UAPI-1 v1.0-rc §2.2、§6 第 5)TANSR_SCHEMA",
    R"TANSR_SCHEMA( 项)。"
    },
    "FacadeCode": {
      "type": "string",
      "enum": [
        "unauthorized",
        "not_found",
        "method_not_allowed",
        "invalid_request",
        "capability_unavailable",
        "capacity_exceeded",
        "precondition_failed",
        "upstream_unavailable"
      ],
      "description": "门面自有码 8(facade.ts ApiFacadeErrorCode;方案 §3.2 第 2 条 + upstream_unavailable;手册 §16.5 门面自有码表)。/api 请求侧三头拒绝(request-headers.ts)不属本词表:其信封携 requestId / detail.domainCode,形同域错误,按 UnifiedError 校验(revision 7 落定,不扩 8 → 12)。"
    },
    "RetryAction": {
      "type": "string",
      "enum": [
        "none",
        "same-request",
        "query-status",
        "rebind",
        "refresh",
        "rediscover"
      ],
      "description": "统一 retryAction 6(RFC §2.3;api-error-map.json retryActions)。"
    },
    "DomainRetryAction": {
      "type": "string",
      "enum": [
        "none",
        "query-status",
        "rebind",
        "same-request",
        "refresh-projection",
        "b)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ackoff",
        "discover",
        "reconcile",
        "refresh"
      ],
      "description": "各族原 retryAction 词表并集(RFC §2.1;保留于 UnifiedError.detail.domainRetryAction,不删任何域动作)。"
    },
    "DomainCode": {
      "type": "string",
      "minLength": 1,
      "maxLength": 64,
      "pattern": "^[a-z][a-z0-9_]*$",
      "description": "原族错误码形(RFC §2.2:统一信封 detail.domainCode 保留原码)。"
    },
    "OperationState": {
      "type": "string",
      "enum": [
        "enabled",
        "disabled",
        "unavailable"
      ],
      "description": "围栏三态(RFC §1.4;方案 §3.6):unavailable = 域未装配或对本会话无能力源(404);disabled = 有能力源但当前不放行(403);enabled = 围栏内。"
    },
    "ClosureOperationName": {
      "type": "string",
      "enum": [
        "session.create",
        "session.list",
        "session.get",
        "session.close",
        "session.message.send",
        "session.events.observe",
        "session.interrupt",
        "session.history.read",
        "session.tool.result",
        "session.permission.decide",
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA( "session.question.answer",
        "session.compact",
        "session.checkpoint.list",
        "session.checkpoint.create",
        "session.checkpoint.import",
        "session.checkpoint.restore",
        "session.checkpoint.export",
        "session.checkpoint.delete",
        "session.cwd.set",
        "session.audio.transcribe",
        "session.audio.speak",
        "session.input.capabilities",
        "session.input.submit",
        "session.input.status",
        "approval.credential.submit",
        "execution.initialize",
        "execution.capabilities",
        "execution.boundary",
        "execution.binding.create",
        "execution.status",
        "executor.register",
        "executor.heartbeat",
        "executor.operations.poll",
        "executor.receipt.submit",
        "archive.capabilities",
        "archive.binding.target",
        "archive.binding.create",
        "archive.binding.get",
        "archive.binding.close",
        "archive.status",
        "archive.events.observe",
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(        "archive.records.read",
        "archive.artifact.read",
        "archive.ack.commit",
        "archive.ack.rebase",
        "archive.operation.query",
        "archive.sync",
        "material.response.submit",
        "material.status",
        "material.upload.status",
        "material.upload.chunk",
        "cache.capabilities",
        "cache.binding.open",
        "cache.binding.get",
        "cache.binding.renew",
        "cache.binding.rebind",
        "cache.binding.rotate",
        "cache.binding.close",
        "cache.operation.query",
        "cache.diagnostics",
        "cache.core.diagnostics",
        "terminal.capabilities",
        "terminal.binding.create",
        "terminal.executor.events.observe",
        "terminal.output.batch",
        "terminal.execution.state",
        "terminal.output.observe",
        "terminal.output.status",
        "terminal.configuration.read",
        "terminal.configuration.commit",
        "terminal.memory.read",
        "terminal.memory.command",
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "terminal.memory.receipt",
        "terminal.observation.resources",
        "terminal.observation.correlations",
        "terminal.profile.catalog",
        "terminal.profile.usage"
      ],
      "description": "围栏操作名 77 = manifest 操作目录去 4 条部署级发现读(discovery.* 与 session.capabilities);事实源 closure.ts CLOSURE_OPERATIONS,目录序。"
    },
    "OperationName": {
      "type": "string",
      "minLength": 3,
      "maxLength": 128,
      "pattern": "^[a-z]+(\\.[a-z][a-z0-9]*)+$",
      "description": "manifest 操作目录名形制(用户拍板 D2 点分风格;generate-manifest.mjs OPERATION_NAME_PATTERN)。"
    },
    "ApiPathTemplate": {
      "type": "string",
      "pattern": "^/api(/(:[A-Za-z]+|[A-Za-z0-9._~-]+))+$",
      "maxLength": 256,
      "description": "/api 路由模板(:id / :targetId / :uploadId / :ticketId 占位;方案 §3.3 路由表)。票据段统一层命名 ticketId(方案 §0 D7 ③;RFC §3):/v2 ⑩⑪ wire 仍为 requestId,manifest 操作目录已用 :ticketId,观测模板占位改名由主线再生。"
    },
    "ApiEntryTemplate": {
      "type": "string",
      "pattern": "^/api(/(:[A-Za-z]+|[A-Za-z0-9._~-]+|)TANSR_SCHEMA",
    R"TANSR_SCHEMA(\\*\\*))*$",
      "maxLength": 256,
      "description": "族入口模板(可含 /** 通配;manifest families[].apiEntries,只作文档)。"
    },
    "LegacyPathTemplate": {
      "type": "string",
      "pattern": "^/v[123](/(:[A-Za-z]+|[A-Za-z0-9._~-]+|\\*\\*))+$",
      "maxLength": 256,
      "description": "内部适配路径模板(/v2、/v3/*;用户拍板 D3:保留为内部入口一个版本周期)。/v1 仅 approval.credential.submit 的 /v1/approvals/:id/credential(T-K11 审批回执,计划 §四 32;generate-manifest.mjs validateOperations 单点放行)。"
    },
    "SchemaRef": {
      "type": "string",
      "pattern": "^[a-z0-9-]+#[A-Za-z][A-Za-z0-9]*$",
      "maxLength": 192,
      "description": "操作请求 / 响应 schema 引用 <族 id>#<定义名>(generate-manifest.mjs SCHEMA_REF_PATTERN)。"
    },
    "Cursor": {
      "type": "string",
      "minLength": 1,
      "maxLength": 512,
      "description": "不透明游标(RFC §1.5 cursorSet 五位之一的取值形;各位只在自身合同条件满足后推进,不得互换)。"
    },
    "ArchiveCoverage": {
      "type": "object",
      "properties": {
        "fromSequence": {
          "$ref": "#/definitions/Sequence"
        },
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "throughSequence": {
          "$ref": "#/definitions/Sequence"
        },
        "headDigest": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "fromSequence",
        "throughSequence",
        "headDigest"
      ],
      "additionalProperties": false,
      "description": "档案覆盖区间(复制自 sdk2-ext-v1 Coverage:闭区间 from<=through,headDigest 为宿主链承诺)。cursorSet.archiveCoverage 在 wire 上的对象形;SSE 游标恒不是档案覆盖。"
    },
    "TerminalStatus": {
      "enum": [
        null,
        "accepted",
        "completed",
        "aborted",
        "unknown"
      ],
      "description": "RFC §1.5:null = 非终态事件;accepted 只表示受理;unknown 按 result_unknown 处置(只许 query-status / rebind)。"
    },
    "EventType": {
      "type": "string",
      "minLength": 1,
      "maxLength": 128,
      "pattern": "^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*)*$",
      "description": "事件类型(小写点分,段内允许 _ 与 -:如 msg.text.delta / material.request / archive.records-available / operation-cancelled);未知 type 的原事件仍原样入 raw,SDK )TANSR_SCHEMA",
    R"TANSR_SCHEMA(类型层暴露 UnknownEvent(RFC §1.5)。"
    },
    "HttpStatus": {
      "type": "integer",
      "enum": [
        400,
        401,
        403,
        404,
        405,
        408,
        409,
        410,
        412,
        413,
        422,
        429,
        500,
        503
      ],
      "description": "统一信封可出线的 HTTP 状态集(RFC §1.2 408 deadline、412 If-Match / 围栏前置;§2.2 各码缺省与备选)。"
    },
    "LimitRange": {
      "type": "object",
      "properties": {
        "min": {
          "type": "integer",
          "minimum": 0
        },
        "max": {
          "type": "integer",
          "minimum": 0
        },
        "default": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 0
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "min",
        "max",
        "default"
      ],
      "additionalProperties": false,
      "description": "manifest capabilities[].value(kind = limit):SDK2 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(限值 TS 区间 × schema default(方案 §3.5;generate-manifest.mjs computeCapabilities ①)。"
    },
    "ClientLock": {
      "type": "object",
      "properties": {
        "client": {
          "type": "string",
          "enum": [
            "node",
            "csharp",
            "go",
            "android",
            "ios",
            "harmony"
          ],
          "description": "各端(方案 §3.5:TypeScript、C#、Go、Android、iOS、鸿蒙)。"
        },
        "repo": {
          "type": "string",
          "pattern": "^tansr-[a-z]+$",
          "maxLength": 64
        },
        "file": {
          "$ref": "#/definitions/RepoPath"
        },
        "lock": {
          "type": "string",
          "minLength": 1,
          "maxLength": 128,
          "description": "登记值:64 hex 指纹或标签(source / generated / hand-written / absent / …);其它仓的锁定文件不在本仓,--check 只据登记值算 drift。"
        },
        "observed": {
          "type": "string",
          "pattern": "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"
        },
        "drift": {
          "type": )TANSR_SCHEMA",
    R"TANSR_SCHEMA("boolean",
          "description": "true = 该端锁定指纹落后主线(如实登记,不代签)。"
        }
      },
      "required": [
        "client",
        "repo",
        "lock",
        "observed",
        "drift"
      ],
      "additionalProperties": false
    },
    "ManifestFamily": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/FamilyId"
        },
        "domains": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/Domain"
          },
          "minItems": 1,
          "uniqueItems": true
        },
        "status": {
          "$ref": "#/definitions/FamilyStatus"
        },
        "rfc": {
          "$ref": "#/definitions/RepoPath"
        },
        "source": {
          "$ref": "#/definitions/RepoPath"
        },
        "sha256": {
          "$ref": "#/definitions/Digest"
        },
        "golden": {
          "type": "object",
          "propertyNames": {
            "$ref": "#/definitions/RepoPath"
          },
          "addition)TANSR_SCHEMA",
    R"TANSR_SCHEMA(alProperties": {
            "$ref": "#/definitions/Digest"
          },
          "description": "golden 文件路径 → 实测 SHA256。"
        },
        "generated": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/RepoPath"
          },
          "uniqueItems": true
        },
        "legacyEntries": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/LegacyPathTemplate"
          },
          "uniqueItems": true
        },
        "apiEntries": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ApiEntryTemplate"
          },
          "uniqueItems": true
        },
        "clients": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ClientLock"
          }
        },
        "requestIdPath": {
          "anyOf": [
            {
              "$ref": "#/definitions/KeyPath"
            },
            {
              "type": "null"
            }
          ],
          "description")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: "体内客户端幂等键位(RFC §1.2 / §3:Idempotency-Key 头映射到域 requestId 的键路径;sdk2 / recovery / cache 族 request.requestId,terminal 三面顶层 requestId);null = 该族体内无此位(agent-session-v1 以墓碑窗去重、archive-sync-v1 请求形未登记、门面自有 / 无 /api 操作的族)。revision 7 加法字段(U7-HDR 三头接线事实源并入 manifest;generate-manifest.mjs FAMILY_REQUEST_ID_PATHS)。"
        },
        "sessionManifestRevision": {
          "type": "integer",
          "minimum": 1,
          "description": "仅 agent-session-v1:packages/sdk/contract/MANIFEST.json revision。"
        },
        "sessionManifestSha256": {
          "$ref": "#/definitions/Digest"
        }
      },
      "required": [
        "id",
        "domains",
        "status",
        "rfc",
        "source",
        "sha256",
        "golden",
        "generated",
        "legacyEntries",
        "apiEntries",
        "clients",
        "requestIdPath"
      ],
      "additionalProperties": false,
      "description": "合同族登记项(方案 §3.5:id / domains / status / rfc / source / sha256 / golden / generated / legacyEntries / )TANSR_SCHEMA",
    R"TANSR_SCHEMA(apiEntries / clients;revision 7 起 + requestIdPath;generate-manifest.mjs FAMILIES → computeFacts)。"
    },
    "ManifestOperation": {
      "type": "object",
      "properties": {
        "name": {
          "$ref": "#/definitions/OperationName"
        },
        "domain": {
          "$ref": "#/definitions/Domain"
        },
        "family": {
          "anyOf": [
            {
              "$ref": "#/definitions/FamilyId"
            },
            {
              "type": "null"
            }
          ]
        },
        "method": {
          "type": "string",
          "enum": [
            "GET",
            "POST",
            "DELETE"
          ]
        },
        "apiPath": {
          "$ref": "#/definitions/ApiPathTemplate"
        },
        "aliases": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ApiPathTemplate"
          },
          "uniqueItems": true,
          "description": "会话作用域别名模板(方案 §3.3;route-table API_SESSION_ALIAS_TEMPLATES 同源)。"
        },
)TANSR_SCHEMA",
    R"TANSR_SCHEMA(        "legacyPath": {
          "anyOf": [
            {
              "$ref": "#/definitions/LegacyPathTemplate"
            },
            {
              "type": "null"
            }
          ]
        },
        "legacyOffloadPath": {
          "anyOf": [
            {
              "type": "string",
              "pattern": "^/v3/sdk2/",
              "maxLength": 256
            },
            {
              "type": "null"
            }
          ],
          "description": "会话 / 执行域的显式卸载族同形路径。"
        },
        "kind": {
          "type": "string",
          "enum": [
            "read",
            "write",
            "stream"
          ]
        },
        "sse": {
          "type": "boolean"
        },
        "request": {
          "anyOf": [
            {
              "$ref": "#/definitions/SchemaRef"
            },
            {
              "type": "null"
            }
          ]
        },
        "response": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sc)TANSR_SCHEMA",
    R"TANSR_SCHEMA(hemaRef"
            },
            {
              "type": "null"
            }
          ]
        },
        "query": {
          "type": "array",
          "items": {
            "type": "string",
            "pattern": "^[A-Za-z][A-Za-z0-9]*$",
            "maxLength": 64
          },
          "uniqueItems": true
        },
        "notes": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 2048
            },
            {
              "type": "null"
            }
          ]
        },
        "etagPath": {
          "anyOf": [
            {
              "$ref": "#/definitions/KeyPath"
            },
            {
              "type": "null"
            }
          ],
          "description": "2xx JSON 响应体内资源版本的键路径(门面据此派生强校验器 ETag: \"<revision>\";RFC §1.2);null = 该操作的响应不是有版本资源的表示,不伪造 ETag。revision 7 加法字段(generate-manifest.mjs VERSIONED_RESOURCES;request-headers.ts 只读 manifest)。"
        },
        "expectedRevision": {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(     "anyOf": [
            {
              "type": "object",
              "properties": {
                "path": {
                  "$ref": "#/definitions/KeyPath"
                },
                "kind": {
                  "type": "string",
                  "enum": [
                    "sequence",
                    "integer"
                  ],
                  "description": "sequence = 十进制序列字符串(sdk2 / cache / 记忆 revision);integer = 非负安全整数(终端配置 revision)。"
                }
              },
              "required": [
                "path",
                "kind"
              ],
              "additionalProperties": false
            },
            {
              "type": "null"
            }
          ],
          "description": "写请求体内 expectedRevision 的键路径与类型(If-Match: \"<revision>\" 映射目标;体内缺席填入、在场且不等 → 412 precondition_failed;RFC §1.2);null = 该操作不接受 If-Match(出现即 400 if_match_not_applicable)。只允许登记在 kind: write 操作上。revision 7 加法字段。"
        }
      },
      "required": [
        "name",
    )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "domain",
        "family",
        "method",
        "apiPath",
        "aliases",
        "legacyPath",
        "legacyOffloadPath",
        "kind",
        "sse",
        "request",
        "response",
        "query",
        "notes",
        "etagPath",
        "expectedRevision"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "domain": {
                "const": "discovery"
              }
            }
          },
          "then": {
            "properties": {
              "family": {
                "type": "null"
              },
              "legacyPath": {
                "type": "null"
              },
              "legacyOffloadPath": {
                "type": "null"
              }
            }
          },
          "else": {
            "properties": {
              "family": {
                "$ref": "#/definitions/FamilyId"
              },
              "legacyPath": {
                "$ref": "#/d)TANSR_SCHEMA",
    R"TANSR_SCHEMA(efinitions/LegacyPathTemplate"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "sse": {
                "const": true
              }
            }
          },
          "then": {
            "properties": {
              "kind": {
                "const": "stream"
              }
            }
          },
          "else": {
            "properties": {
              "kind": {
                "enum": [
                  "read",
                  "write"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "method": {
                "const": "GET"
              }
            }
          },
          "then": {
            "properties": {
              "kind": {
                "enum": [
                  "read",
                  "stream"
                ]
              }
            }
          },
          "else": {
            "properties": {
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "kind": {
                "const": "write"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "enum": [
                  "read",
                  "stream"
                ]
              }
            }
          },
          "then": {
            "properties": {
              "expectedRevision": {
                "type": "null"
              }
            }
          }
        }
      ],
      "description": "操作目录项(方案 §3.5 manifest v2;generate-manifest.mjs operation()):门面自有操作 family / legacyPath 恒 null;kind 由 method / sse 推导;revision 7 起 + etagPath / expectedRevision(If-Match 仅写操作 → 读 / 流操作 expectedRevision 恒 null)。"
    },
    "ManifestCapability": {
      "type": "object",
      "properties": {
        "id": {
          "type": "string",
          "minLength": 3,
          "maxLength": 128,
          "pattern": "^[a-z]+(\\.[A-Za-z0-9][A-Za-z0-9-]*)+$"
        },
        "domain": {
          "$re)TANSR_SCHEMA",
    R"TANSR_SCHEMA(f": "#/definitions/ClosureDomain"
        },
        "family": {
          "$ref": "#/definitions/FamilyId"
        },
        "kind": {
          "type": "string",
          "enum": [
            "limit",
            "feature",
            "capability"
          ]
        },
        "source": {
          "type": "string",
          "pattern": "^[^#\\s]+#[A-Za-z_][A-Za-z0-9_]*$",
          "maxLength": 256
        },
        "value": {
          "anyOf": [
            {
              "$ref": "#/definitions/LimitRange"
            },
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 256
            }
          ]
        }
      },
      "required": [
        "id",
        "domain",
        "family",
        "kind",
        "source",
        "value"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "kind": {
                "const": "limit"
              }
            }
       )TANSR_SCHEMA",
    R"TANSR_SCHEMA(   },
          "then": {
            "properties": {
              "value": {
                "$ref": "#/definitions/LimitRange"
              }
            }
          },
          "else": {
            "properties": {
              "value": {
                "type": "string"
              }
            }
          }
        }
      ],
      "description": "能力项(方案 §3.5:只登记机器可派生的限值 / feature / 能力名;权限、计量、撤销不编造)。"
    },
    "PlatformApiReference": {
      "type": "object",
      "properties": {
        "id": {
          "$ref": "#/definitions/FamilyId"
        },
        "doc": {
          "$ref": "#/definitions/RepoPath"
        },
        "host": {
          "type": "string",
          "pattern": "^tansr-[a-z]+$",
          "maxLength": 64
        },
        "entries": {
          "type": "array",
          "items": {
            "type": "string",
            "pattern": "^/",
            "maxLength": 256
          },
          "minItems": 1,
          "uniqueItems": true
        },
        "vendored": {
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "anyOf": [
            {
              "$ref": "#/definitions/RepoPath"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "id",
        "doc",
        "host",
        "entries",
        "vendored"
      ],
      "additionalProperties": false,
      "description": "平台 API 能力族引用(用户拍板 D1:tansr-api 路由不进 /api dispatcher,只以引用登记)。"
    },
    "InstalledDomains": {
      "type": "object",
      "properties": {
        "session": {
          "anyOf": [
            {
              "$ref": "#/definitions/SessionFamily"
            },
            {
              "type": "null"
            }
          ],
          "description": "首选会话族;null = 未装配会话面。"
        },
        "execution": {
          "type": "boolean"
        },
        "archive": {
          "type": "boolean"
        },
        "archive-sync": {
          "type": "boolean"
        },
        "cache": {
          "type": "boolean"
        },
        "terminal": {
          ")TANSR_SCHEMA",
    R"TANSR_SCHEMA(type": "boolean"
        },
        "terminal-observation": {
          "type": "boolean"
        },
        "terminal-profile": {
          "type": "boolean"
        }
      },
      "required": [
        "session",
        "execution",
        "archive",
        "archive-sync",
        "cache",
        "terminal",
        "terminal-observation",
        "terminal-profile"
      ],
      "additionalProperties": false,
      "description": "本部署已装配的域(facade.ts ApiInstalledDomains;由实际构造出的 handler 推导,不读配置猜测)。"
    },
    "Manifest": {
      "type": "object",
      "properties": {
        "format": {
          "const": "tansr-api-manifest-v1"
        },
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "revision": {
          "type": "integer",
          "minimum": 1,
          "description": "任一族 SHA / 状态 / 入口、操作目录、能力项变化 → +1(加法亦 +1;方案 §3.7)。"
        },
        "schemaHash": {
          "$ref": "#/definitions/Digest"
        },
        "families": {
          "type": "array",
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "items": {
            "$ref": "#/definitions/ManifestFamily"
          },
          "minItems": 1
        },
        "operations": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ManifestOperation"
          },
          "minItems": 1
        },
        "capabilities": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/ManifestCapability"
          }
        },
        "platformApi": {
          "type": "array",
          "items": {
            "$ref": "#/definitions/PlatformApiReference"
          }
        },
        "runtime": {
          "type": "object",
          "properties": {
            "installed": {
              "$ref": "#/definitions/InstalledDomains"
            }
          },
          "required": [
            "installed"
          ],
          "additionalProperties": false,
          "description": "仅运行时 GET /api/manifest 附带(facade.ts manifestBody);仓内 api-manifest.json 无此段。"
        }
      },
      "required": )TANSR_SCHEMA",
    R"TANSR_SCHEMA([
        "format",
        "contract",
        "revision",
        "schemaHash",
        "families",
        "operations",
        "capabilities",
        "platformApi"
      ],
      "additionalProperties": false,
      "description": "Capability Manifest(方案 §3.5;RFC §1.1 tansr-manifest-revision 的来源)。仓内产物 packages/server/contract/api-manifest.json 与 GET /api/manifest 响应体(多 runtime.installed)均须通过。"
    },
    "DomainCapability": {
      "type": "object",
      "properties": {
        "installed": {
          "type": "boolean",
          "description": "本部署装配了该域没有——不是“你有权用”(方案 §3.6;手册 §16.3)。"
        },
        "contract": {
          "$ref": "#/definitions/FamilyId"
        },
        "status": {
          "type": "string",
          "enum": [
            "frozen",
            "final",
            "additive",
            "candidate",
            "drafted",
            "unregistered"
          ]
        },
        "schemaHash": {
          "$ref": "#/definitions/SchemaHash"
        }
      },
      "required)TANSR_SCHEMA",
    R"TANSR_SCHEMA(": [
        "installed",
        "contract",
        "status",
        "schemaHash"
      ],
      "additionalProperties": false,
      "description": "部署级域视图(facade.ts capabilitiesBody)。additionalProperties:false 即禁止任何 URL 导航字段(entry / capabilities;RFC-SDK2-1 §10.5;计划 §四 9)。"
    },
    "SessionDomainCapability": {
      "type": "object",
      "properties": {
        "installed": {
          "type": "boolean"
        },
        "family": {
          "type": "object",
          "properties": {
            "preferred": {
              "anyOf": [
                {
                  "$ref": "#/definitions/SessionFamily"
                },
                {
                  "type": "null"
                }
              ],
              "description": "缺省会话族(未携 tansr-session-family 时生效)。"
            },
            "available": {
              "type": "array",
              "items": {
                "$ref": "#/definitions/SessionFamily"
              },
              "uniqueItems": true,
              "maxIte)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ms": 2,
              "description": "本部署实际装配的族。"
            }
          },
          "required": [
            "preferred",
            "available"
          ],
          "additionalProperties": false
        },
        "contract": {
          "$ref": "#/definitions/FamilyId"
        },
        "status": {
          "type": "string",
          "enum": [
            "frozen",
            "final",
            "additive",
            "candidate",
            "drafted",
            "unregistered"
          ]
        },
        "schemaHash": {
          "$ref": "#/definitions/SchemaHash"
        }
      },
      "required": [
        "installed",
        "family",
        "contract",
        "status",
        "schemaHash"
      ],
      "additionalProperties": false,
      "description": "session 域另含 family{preferred, available[]}(方案 §3.3 / D6 第 9 项按 B)。"
    },
    "Capabilities": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA("manifestRevision": {
          "type": "integer",
          "minimum": 1
        },
        "schemaHash": {
          "$ref": "#/definitions/Sha256Prefixed"
        },
        "domains": {
          "type": "object",
          "properties": {
            "session": {
              "allOf": [
                {
                  "$ref": "#/definitions/SessionDomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "agent-session-v1"
                    }
                  }
                }
              ]
            },
            "execution": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "sdk2-ext-v1"
                    }
                  }
                }
              ]
            },
            "archive": {
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "sdk2-ext-v1"
                    }
                  }
                }
              ]
            },
            "archive-sync": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "archive-sync-v1"
                    }
                  }
                }
              ]
            },
            "cache": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "sdk2-cache-v1"
                    }
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        }
                }
              ]
            },
            "terminal": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "terminal-services-v1"
                    }
                  }
                }
              ]
            },
            "terminal-observation": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "properties": {
                    "contract": {
                      "const": "terminal-observation-v1"
                    }
                  }
                }
              ]
            },
            "terminal-profile": {
              "allOf": [
                {
                  "$ref": "#/definitions/DomainCapability"
                },
                {
                  "pr)TANSR_SCHEMA",
    R"TANSR_SCHEMA(operties": {
                    "contract": {
                      "const": "terminal-profile-v1"
                    }
                  }
                }
              ]
            }
          },
          "required": [
            "session",
            "execution",
            "archive",
            "archive-sync",
            "cache",
            "terminal",
            "terminal-observation",
            "terminal-profile"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "contract",
        "manifestRevision",
        "schemaHash",
        "domains"
      ],
      "additionalProperties": false,
      "description": "GET /api/capabilities 部署级发现(方案 §3.3 / §3.5;facade.ts capabilitiesBody):域可用性、合同族与修订指纹、会话族 preferred / available。不含任何 URL 导航字段——SDK 恒用 manifest 生成的固定路径(RFC-SDK2-1 §10.5)。各域 contract 为 route-table.ts API_DOMAIN_FAMILY 常量。"
    },
    "ClosureDomainState": {
      "type": "object",
      "properties": {
        "installed": {
          "typ)TANSR_SCHEMA",
    R"TANSR_SCHEMA(e": "boolean"
        },
        "revision": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1,
              "maxLength": 512
            },
            {
              "type": "null"
            }
          ],
          "description": "该域自报代际(执行 = executionBoundary.revision、档案 = operationEpoch.id、终端 = schemaRevision、会话 = 族、缓存 = protocol;其余 null)(RFC §1.4)。"
        }
      },
      "required": [
        "installed",
        "revision"
      ],
      "additionalProperties": false
    },
    "CapabilityClosure": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "closureId": {
          "allOf": [
            {
              "$ref": "#/definitions/Digest"
            }
          ],
          "description": "sha256(UTF8(\"tansr.unified.closure.v1\") ‖ 0x00 ‖ canonical({ authorizationRevision, domains, operations }))(RFC §1.4 / §4.2);不含时间,同输入同 id;响应头 tansr-closure-id 同值。"
        },
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "authorizationRevision": {
          "anyOf": [
            {
              "$ref": "#/definitions/Sequence"
            },
            {
              "type": "null"
            }
          ],
          "description": "授权代际(执行宿主 readPolicy().authorizationRevision;RFC §3 分名 authorizationRevision(Sequence));无策略源 = null。"
        },
        "domains": {
          "type": "object",
          "properties": {
            "session": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "execution": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "archive": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "archive-sync": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "cache": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "terminal": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "terminal)TANSR_SCHEMA",
    R"TANSR_SCHEMA(-observation": {
              "$ref": "#/definitions/ClosureDomainState"
            },
            "terminal-profile": {
              "$ref": "#/definitions/ClosureDomainState"
            }
          },
          "required": [
            "session",
            "execution",
            "archive",
            "archive-sync",
            "cache",
            "terminal",
            "terminal-observation",
            "terminal-profile"
          ],
          "additionalProperties": false
        },
        "operations": {
          "type": "object",
          "properties": {
            "session.create": {
              "$ref": "#/definitions/OperationState"
            },
            "session.list": {
              "$ref": "#/definitions/OperationState"
            },
            "session.get": {
              "$ref": "#/definitions/OperationState"
            },
            "session.close": {
              "$ref": "#/definitions/OperationState"
            },
            "session.message.send": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "$ref": "#/definitions/OperationState"
            },
            "session.events.observe": {
              "$ref": "#/definitions/OperationState"
            },
            "session.interrupt": {
              "$ref": "#/definitions/OperationState"
            },
            "session.history.read": {
              "$ref": "#/definitions/OperationState"
            },
            "session.tool.result": {
              "$ref": "#/definitions/OperationState"
            },
            "session.permission.decide": {
              "$ref": "#/definitions/OperationState"
            },
            "session.question.answer": {
              "$ref": "#/definitions/OperationState"
            },
            "session.compact": {
              "$ref": "#/definitions/OperationState"
            },
            "session.checkpoint.list": {
              "$ref": "#/definitions/OperationState"
            },
            "session.checkpoint.create": {
              "$ref": "#/definitions/OperationState"
            },
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           "session.checkpoint.import": {
              "$ref": "#/definitions/OperationState"
            },
            "session.checkpoint.restore": {
              "$ref": "#/definitions/OperationState"
            },
            "session.checkpoint.export": {
              "$ref": "#/definitions/OperationState"
            },
            "session.checkpoint.delete": {
              "$ref": "#/definitions/OperationState"
            },
            "session.cwd.set": {
              "$ref": "#/definitions/OperationState"
            },
            "session.audio.transcribe": {
              "$ref": "#/definitions/OperationState"
            },
            "session.audio.speak": {
              "$ref": "#/definitions/OperationState"
            },
            "session.input.capabilities": {
              "$ref": "#/definitions/OperationState"
            },
            "session.input.submit": {
              "$ref": "#/definitions/OperationState"
            },
            "session.input.status": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "$ref": "#/definitions/OperationState"
            },
            "approval.credential.submit": {
              "$ref": "#/definitions/OperationState"
            },
            "execution.initialize": {
              "$ref": "#/definitions/OperationState"
            },
            "execution.capabilities": {
              "$ref": "#/definitions/OperationState"
            },
            "execution.boundary": {
              "$ref": "#/definitions/OperationState"
            },
            "execution.binding.create": {
              "$ref": "#/definitions/OperationState"
            },
            "execution.status": {
              "$ref": "#/definitions/OperationState"
            },
            "executor.register": {
              "$ref": "#/definitions/OperationState"
            },
            "executor.heartbeat": {
              "$ref": "#/definitions/OperationState"
            },
            "executor.operations.poll": {
              "$ref": "#/definitions/OperationState"
            },
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(       "executor.receipt.submit": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.capabilities": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.binding.target": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.binding.create": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.binding.get": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.binding.close": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.status": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.events.observe": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.records.read": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.artifact.read": {
              "$ref": "#/definiti)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ons/OperationState"
            },
            "archive.ack.commit": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.ack.rebase": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.operation.query": {
              "$ref": "#/definitions/OperationState"
            },
            "archive.sync": {
              "$ref": "#/definitions/OperationState"
            },
            "material.response.submit": {
              "$ref": "#/definitions/OperationState"
            },
            "material.status": {
              "$ref": "#/definitions/OperationState"
            },
            "material.upload.status": {
              "$ref": "#/definitions/OperationState"
            },
            "material.upload.chunk": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.capabilities": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.binding.open": {
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(      "$ref": "#/definitions/OperationState"
            },
            "cache.binding.get": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.binding.renew": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.binding.rebind": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.binding.rotate": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.binding.close": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.operation.query": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.diagnostics": {
              "$ref": "#/definitions/OperationState"
            },
            "cache.core.diagnostics": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.capabilities": {
              "$ref": "#/definitions/OperationState"
            },
            "t)TANSR_SCHEMA",
    R"TANSR_SCHEMA(erminal.binding.create": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.executor.events.observe": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.output.batch": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.execution.state": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.output.observe": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.output.status": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.configuration.read": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.configuration.commit": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.memory.read": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.memory.command": {
  )TANSR_SCHEMA",
    R"TANSR_SCHEMA(            "$ref": "#/definitions/OperationState"
            },
            "terminal.memory.receipt": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.observation.resources": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.observation.correlations": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.profile.catalog": {
              "$ref": "#/definitions/OperationState"
            },
            "terminal.profile.usage": {
              "$ref": "#/definitions/OperationState"
            }
          },
          "required": [
            "session.create",
            "session.list",
            "session.get",
            "session.close",
            "session.message.send",
            "session.events.observe",
            "session.interrupt",
            "session.history.read",
            "session.tool.result",
            "session.permission.decide",
            "session.questi)TANSR_SCHEMA",
    R"TANSR_SCHEMA(on.answer",
            "session.compact",
            "session.checkpoint.list",
            "session.checkpoint.create",
            "session.checkpoint.import",
            "session.checkpoint.restore",
            "session.checkpoint.export",
            "session.checkpoint.delete",
            "session.cwd.set",
            "session.audio.transcribe",
            "session.audio.speak",
            "session.input.capabilities",
            "session.input.submit",
            "session.input.status",
            "approval.credential.submit",
            "execution.initialize",
            "execution.capabilities",
            "execution.boundary",
            "execution.binding.create",
            "execution.status",
            "executor.register",
            "executor.heartbeat",
            "executor.operations.poll",
            "executor.receipt.submit",
            "archive.capabilities",
            "archive.binding.target",
            "archive.binding.create",
            "archive.binding.get",
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(           "archive.binding.close",
            "archive.status",
            "archive.events.observe",
            "archive.records.read",
            "archive.artifact.read",
            "archive.ack.commit",
            "archive.ack.rebase",
            "archive.operation.query",
            "archive.sync",
            "material.response.submit",
            "material.status",
            "material.upload.status",
            "material.upload.chunk",
            "cache.capabilities",
            "cache.binding.open",
            "cache.binding.get",
            "cache.binding.renew",
            "cache.binding.rebind",
            "cache.binding.rotate",
            "cache.binding.close",
            "cache.operation.query",
            "cache.diagnostics",
            "cache.core.diagnostics",
            "terminal.capabilities",
            "terminal.binding.create",
            "terminal.executor.events.observe",
            "terminal.output.batch",
            "terminal.execution.state",
            "t)TANSR_SCHEMA",
    R"TANSR_SCHEMA(erminal.output.observe",
            "terminal.output.status",
            "terminal.configuration.read",
            "terminal.configuration.commit",
            "terminal.memory.read",
            "terminal.memory.command",
            "terminal.memory.receipt",
            "terminal.observation.resources",
            "terminal.observation.correlations",
            "terminal.profile.catalog",
            "terminal.profile.usage"
          ],
          "additionalProperties": false,
          "description": "77 条操作 → 三态(closure.ts CLOSURE_OPERATIONS 目录序;显式属性 + required 保证恒全覆盖、无未知操作名)。"
        }
      },
      "required": [
        "contract",
        "closureId",
        "authorizationRevision",
        "domains",
        "operations"
      ],
      "additionalProperties": false,
      "description": "GET /api/sessions/:id/capabilities 会话级能力闭合围栏(RFC §1.4;方案 §3.6;closure.ts CapabilityClosure)。联邦各域既有围栏事实,不新造授权引擎;写操作携 tansr-closure-id 作前置(412 precondition_failed / closure_stale)。"
    },
    "FacadeErrorDeta)TANSR_SCHEMA",
    R"TANSR_SCHEMA(il": {
      "type": "object",
      "properties": {
        "reason": {
          "type": "string",
          "enum": [
            "not_installed",
            "outside_closure"
          ],
          "description": "not_installed = 域 / 会话族未装配或已装配域对该资源无人受理;outside_closure = 围栏复核越界(RFC §1.4)。"
        },
        "header": {
          "type": "string",
          "enum": [
            "tansr-session-family",
            "tansr-closure-id",
            "tansr-event-envelope"
          ],
          "description": "触发该错误的请求头名(tansr-event-envelope:协商词表外 → 400 invalid_request,event-envelope.ts;U3-ALIGN 联合断言补入)。"
        },
        "domainCode": {
          "type": "string",
          "enum": [
            "closure_stale"
          ],
          "description": "precondition_failed 的域码(RFC §2.2 门面自有码段)。"
        },
        "closureId": {
          "$ref": "#/definitions/Digest"
        },
        "operation": {
          "$ref": "#/definitions/ClosureOperationName"
        },
        "state": {
          "type": "stri)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ng",
          "enum": [
            "disabled",
            "unavailable"
          ],
          "description": "越围栏时该操作的三态(disabled → 403,unavailable → 404)。"
        }
      },
      "additionalProperties": true,
      "description": "开放对象;只含机器可读原因字段,登记已知键(facade.ts sendFacadeError 各调用点)。"
    },
    "FacadeError": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "traceId": {
          "$ref": "#/definitions/TraceId"
        },
        "requestId": {
          "type": "null",
          "description": "恒 null:门面自有错误无请求体,没有客户端幂等键(RFC §1.3 / §3)。"
        },
        "code": {
          "$ref": "#/definitions/FacadeCode"
        },
        "status": {
          "type": "integer",
          "enum": [
            400,
            401,
            403,
            404,
            405,
            412,
            503
          ]
        },
        "retryAction": {
          "$ref": "#/definitions/RetryAction"
        },
        "m)TANSR_SCHEMA",
    R"TANSR_SCHEMA(essage": {
          "type": "string",
          "minLength": 1,
          "maxLength": 1024,
          "description": "不含路径、凭据、endUserId、sessionId。"
        },
        "detail": {
          "$ref": "#/definitions/FacadeErrorDetail"
        }
      },
      "required": [
        "contract",
        "traceId",
        "requestId",
        "code",
        "status",
        "retryAction",
        "message"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "code": {
                "const": "unauthorized"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  401
                ]
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "not_found"
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  404
                ]
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "method_not_allowed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  405
                ]
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "invalid_request"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(                 400
                ]
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capability_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  404,
                  403
                ]
              },
              "retryAction": {
                "const": "none"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capacity_exceeded"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  503
                ]
              },
              "retryAction": {
                "const": "s)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ame-request"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "precondition_failed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  412
                ]
              },
              "retryAction": {
                "const": "rediscover"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "upstream_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  503
                ]
              },
              "retryAction": {
                "const": "same-request"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             "code": {
                "const": "precondition_failed"
              }
            }
          },
          "then": {
            "required": [
              "detail"
            ],
            "properties": {
              "detail": {
                "required": [
                  "domainCode",
                  "closureId"
                ],
                "properties": {
                  "domainCode": {
                    "const": "closure_stale"
                  }
                }
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "status": {
                "const": 403
              }
            }
          },
          "then": {
            "required": [
              "detail"
            ],
            "properties": {
              "detail": {
                "required": [
                  "reason",
                  "operation",
                  "state",
                  "closureId"
                ],
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(             "properties": {
                  "reason": {
                    "const": "outside_closure"
                  },
                  "state": {
                    "const": "disabled"
                  }
                }
              }
            }
          }
        },
        {
          "if": {
            "required": [
              "detail"
            ],
            "properties": {
              "status": {
                "const": 404
              },
              "detail": {
                "required": [
                  "reason"
                ],
                "properties": {
                  "reason": {
                    "const": "outside_closure"
                  }
                }
              }
            }
          },
          "then": {
            "properties": {
              "detail": {
                "required": [
                  "operation",
                  "state",
                  "closureId"
                ],
                "properties": {
          )TANSR_SCHEMA",
    R"TANSR_SCHEMA(        "state": {
                    "const": "unavailable"
                  }
                }
              }
            }
          }
        }
      ],
      "description": "门面自有错误信封(RFC §1.3;方案 §3.2 第 2 条;facade.ts sendFacadeError):{ contract, traceId, requestId:null, code, status, retryAction, message, detail? }。码 ↔ 状态 ↔ retryAction 绑定 = facade.ts ERROR_STATUS / ERROR_RETRY;capability_unavailable 越围栏时 403(RFC §1.4)。"
    },
    "UnifiedErrorDetail": {
      "type": "object",
      "properties": {
        "domain": {
          "$ref": "#/definitions/Domain"
        },
        "family": {
          "$ref": "#/definitions/FamilyId"
        },
        "domainCode": {
          "allOf": [
            {
              "$ref": "#/definitions/DomainCode"
            }
          ],
          "description": "原族码(RFC §2.2)。"
        },
        "domainStatus": {
          "type": "integer",
          "minimum": 100,
          "maximum": 599,
          "description": "原族 wire 状态(RFC §2.2:统一缺省出线、原状态留此)。"
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(},
        "domainRetryAction": {
          "anyOf": [
            {
              "$ref": "#/definitions/DomainRetryAction"
            },
            {
              "type": "null"
            }
          ],
          "description": "原族 retryAction(RFC §2.2 / §2.3);原族信封无动作位(/v2 agent-session-v1 族)→ null(error-envelope.ts 真实产物;U3-ALIGN 联合断言补入)。"
        },
        "fallback": {
          "type": "string",
          "enum": [
            "none",
            "legacy-cold"
          ],
          "description": "cache 族 fallback 只保留于此,不进统一层(RFC §2.2 / §5:统一合同禁止静默降级)。"
        },
        "reason": {
          "type": "string",
          "enum": [
            "not_installed",
            "outside_closure",
            "idempotency_key_invalid",
            "idempotency_key_not_applicable",
            "idempotency_key_reused",
            "idempotency_key_mismatch",
            "receipt_not_retained",
            "receipt_window_full",
            "if_match_invalid",
            "if_match_not_applicable",
        )TANSR_SCHEMA",
    R"TANSR_SCHEMA(    "if_match_body_mismatch",
            "if_match_stale",
            "deadline_invalid",
            "deadline_exceeded",
            "header_body_limit",
            "header_body_not_canonical",
            "header_processing_failed"
          ],
          "description": "机器可读细因。not_installed / outside_closure 为围栏与装配(RFC §1.4);其余 15 项为 /api 请求侧三头接线(RFC §1.2;方案 §0 D27 零新码,request-headers.ts;revision 7 登记):Idempotency-Key 形 / 适用 / 同键异义 / 体内 requestId 不等 / 原回执未保留 / 回执窗全在飞,If-Match 形 / 适用 / 体内 expectedRevision 不等 / 域版本陈旧(412 refresh),deadline 形 / 已过期(408),三头体处理超帽(413)/ 重编失败 / 处理异常。"
        },
        "header": {
          "type": "string",
          "pattern": "^[a-z][a-z0-9-]*$",
          "maxLength": 64,
          "description": "触发该错误的请求头名(Idempotency-Key / If-Match / deadline / tansr-*;RFC §1.2)。"
        },
        "limitBytes": {
          "type": "integer",
          "minimum": 1,
          "description": "header_body_limit 的体帽(字节;RFC §1.2 三头体处理上限)。"
        },
        "closureId": {
          "$ref")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: "#/definitions/Digest"
        },
        "operation": {
          "$ref": "#/definitions/ClosureOperationName"
        },
        "state": {
          "$ref": "#/definitions/OperationState"
        }
      },
      "additionalProperties": true,
      "description": "开放对象;登记已知键。domainCode / domainStatus / domainRetryAction / fallback 为 RFC §2.2 保留位。"
    },
    "UnifiedError": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "traceId": {
          "$ref": "#/definitions/TraceId"
        },
        "requestId": {
          "anyOf": [
            {
              "$ref": "#/definitions/IdempotencyKey"
            },
            {
              "type": "null"
            }
          ],
          "description": "客户端幂等键(Idempotency-Key 头或体内 request.requestId);请求无键 = null(RFC §1.3 / §3)。"
        },
        "code": {
          "$ref": "#/definitions/UnifiedCode"
        },
        "status": {
          "$ref": "#/definitions/HttpSta)TANSR_SCHEMA",
    R"TANSR_SCHEMA(tus"
        },
        "retryAction": {
          "$ref": "#/definitions/RetryAction"
        },
        "retryAfterMs": {
          "type": "integer",
          "minimum": 1,
          "description": "same-request 在场时须等待;上限由 manifest 约束(RFC §1.2 / §2.3)。"
        },
        "message": {
          "type": "string",
          "minLength": 1,
          "maxLength": 1024
        },
        "detail": {
          "$ref": "#/definitions/UnifiedErrorDetail"
        }
      },
      "required": [
        "contract",
        "traceId",
        "requestId",
        "code",
        "status",
        "retryAction",
        "message"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "code": {
                "const": "invalid_request"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  400,
                  408,
                  4)TANSR_SCHEMA",
    R"TANSR_SCHEMA(22
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "protocol_mismatch"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  400
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "unauthorized"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  401
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "forbidden"
              }
            }
          },
          "then": {
            "properties": {
              "status": {)TANSR_SCHEMA",
    R"TANSR_SCHEMA(
                "enum": [
                  403
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "not_found"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  404
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "method_not_allowed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  405
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "gone"
              }
            }
          },
          "then": {
            "p)TANSR_SCHEMA",
    R"TANSR_SCHEMA(roperties": {
              "status": {
                "enum": [
                  410
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "conflict"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  409,
                  412
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "stale_generation"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  409
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "gap"
              }
   )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  409,
                  410
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capability_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  404,
                  403
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "capacity_exceeded"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  429,
                  503
                ]
              }
            }
          }
        },
        {
 )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         "if": {
            "properties": {
              "code": {
                "const": "payload_too_large"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  413
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "upstream_unavailable"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  503
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "result_unknown"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  503
                ]
     )TANSR_SCHEMA",
    R"TANSR_SCHEMA(         }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "rejected"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  422
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "internal_error"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  500
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "precondition_failed"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum")TANSR_SCHEMA",
    R"TANSR_SCHEMA(: [
                  412
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "not_canonical"
              }
            }
          },
          "then": {
            "properties": {
              "status": {
                "enum": [
                  400
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "result_unknown"
              }
            }
          },
          "then": {
            "properties": {
              "retryAction": {
                "enum": [
                  "query-status",
                  "rebind"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "precondition_failed"
              }
            }
          })TANSR_SCHEMA",
    R"TANSR_SCHEMA(,
          "then": {
            "properties": {
              "retryAction": {
                "enum": [
                  "rediscover",
                  "refresh"
                ]
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "code": {
                "const": "not_canonical"
              }
            }
          },
          "then": {
            "properties": {
              "retryAction": {
                "enum": [
                  "none"
                ]
              }
            }
          }
        }
      ],
      "description": "阶段三域错误统一信封(RFC-UAPI-1 v1.0-rc §2.2 / §2.3;方案 §0 D7、§4 第 2 条):code 取 19 统一码(17 四族映射码 + precondition_failed + not_canonical),HTTP 取该码缺省(表首值)或 RFC 表列备选;原族码 / 状态 / 动作保留于 detail;result_unknown 恒只许 query-status / rebind(不得换键重放);precondition_failed 恒 412,retryAction 为 rediscover(围栏陈旧 closure_stale)或 refresh(If-Match 资源版本陈旧 if_match_stale / if_match_body_mismatch,revision 7;RFC §1.2);not_canonica)TANSR_SCHEMA",
    R"TANSR_SCHEMA(l 恒 400 / none(严格 canonical 入口拒收)。/api 请求侧三头拒绝(request-headers.ts:invalid_request / conflict / precondition_failed / payload_too_large / result_unknown / internal_error,携 requestId = Idempotency-Key 或 null)按本定义校验,不扩 FacadeCode(U7-HDR 报告 §8.2 建议;revision 7 落定)。"
    },
    "CursorSet": {
      "type": "object",
      "properties": {
        "eventCursor": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "type": "null"
            }
          ],
          "description": "事件流位置(SSE Last-Event-ID 续订)。"
        },
        "archiveCoverage": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "$ref": "#/definitions/ArchiveCoverage"
            },
            {
              "type": "null"
            }
          ],
          "description": "档案已覆盖序列(ACK coverage):wire 上为 archive.status.payload.acknowledgedCoverage 的 Coverage 对象(sdk2-ext-v1 Coverage,含 headDigest);不透明游标串形)TANSR_SCHEMA",
    R"TANSR_SCHEMA(保留给其它域合同。"
        },
        "outputWatermark": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "type": "null"
            }
          ],
          "description": "终端输出水位(afterSeq)。"
        },
        "materialConsumed": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "type": "null"
            }
          ],
          "description": "材料已被核心消费(≠ received)。"
        },
        "ackReceipt": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "type": "null"
            }
          ],
          "description": "ACK 回执。"
        }
      },
      "required": [
        "eventCursor",
        "archiveCoverage",
        "outputWatermark",
        "materialConsumed",
        "ackReceipt"
      ],
      "additionalProperties": false,
      "description": "RFC §1.5:五位分离,不得互换为普通字符串;各位只在自身合同条件满足后推进)TANSR_SCHEMA",
    R"TANSR_SCHEMA((SSE EOF、HTTP 202、连接成功均不推进)。"
    },
    "EventEnvelope": {
      "type": "object",
      "properties": {
        "contract": {
          "$ref": "#/definitions/Contract"
        },
        "eventId": {
          "anyOf": [
            {
              "$ref": "#/definitions/Cursor"
            },
            {
              "type": "null"
            }
          ],
          "description": "本帧 SSE id:(缺席 null;服务端原样透传,不改写、不合成)。与 cursorSet.eventCursor 同源:前者是帧身份,后者是 Last-Event-ID 续订游标位。"
        },
        "domain": {
          "$ref": "#/definitions/Domain"
        },
        "type": {
          "anyOf": [
            {
              "$ref": "#/definitions/EventType"
            },
            {
              "type": "null"
            }
          ],
          "description": "事件类型 = SSE event: 字段,缺席取 raw.type / raw.eventType;服务端无法判定 → null(视为未知事件,原文在 raw)。"
        },
        "cursorSet": {
          "$ref": "#/definitions/CursorSet"
        },
        "terminalStatus": {
          "$ref": "#/definitions/Termin)TANSR_SCHEMA",
    R"TANSR_SCHEMA(alStatus"
        },
        "raw": {
          "type": "object",
          "description": "原事件对象(RFC §1.5 D18):每帧恒为原 data: 的 JSON 对象原样(服务端不 parse→stringify 往返),按 type 解析;未知 type 仍原样入 raw(对抗单 A12)。不再有 payload 位。"
        }
      },
      "required": [
        "contract",
        "eventId",
        "domain",
        "type",
        "cursorSet",
        "terminalStatus",
        "raw"
      ],
      "additionalProperties": false,
      "description": "统一事件包络(RFC §1.5;方案 §0 D18 定形 7 键 { contract, eventId, domain, type, cursorSet, terminalStatus, raw },键序即 wire 序;阶段三实施,仅新 SDK ./api 子入口经 tansr-event-envelope: unified-v1 协商后消费;旧 SSE 帧字节不变)。控制信封严格(七键之外 / 缺键 → invalid_envelope);不出 seq(位置由 cursorSet.eventCursor 表达)、不出 payload(raw 恒为原事件对象)。"
    },
    "RequestHeaders": {
      "type": "object",
      "properties": {
        "authorization": {
          "type": "string",
          "pattern": "^Bearer [\\x21-\\x7E]+$",
          "maxLength": 8192,
          "description": "多租鉴权(已有,不变;RFC §1.2)。"
        },
        "trac)TANSR_SCHEMA",
    R"TANSR_SCHEMA(eparent": {
          "type": "string",
          "pattern": "^[0-9a-f]{2}-[0-9a-f]{32}-[0-9a-f]{16}-[0-9a-f]{2}$",
          "description": "W3C traceparent(version 00 形);观测 requestId 采纳序第二位。"
        },
        "x-request-id": {
          "$ref": "#/definitions/TraceId"
        },
        "idempotency-key": {
          "allOf": [
            {
              "$ref": "#/definitions/IdempotencyKey"
            }
          ],
          "description": "写操作幂等键(RFC §1.2;阶段三接线,今日不被读取)。"
        },
        "if-match": {
          "type": "string",
          "pattern": "^(W/)?\"[\\x21\\x23-\\x7E]*\"$",
          "maxLength": 1024,
          "description": "RFC 7232 entity-tag;映射到各域 expectedRevision,不匹配 → 412 conflict(RFC §1.2)。"
        },
        "deadline": {
          "type": "string",
          "format": "date-time",
          "description": "RFC-3339 请求截止;过期 → 408 invalid_request;重试不得延长(RFC §1.2)。"
        },
        "tansr-session-family": {
          "$ref": "#/definitions/SessionFamily"
        },
        "ta)TANSR_SCHEMA",
    R"TANSR_SCHEMA(nsr-closure-id": {
          "allOf": [
            {
              "$ref": "#/definitions/Digest"
            }
          ],
          "description": "围栏前置条件 = 上次围栏 closureId;不等 → 412 precondition_failed;出现在不适用请求 → 400(RFC §1.2 / §1.4)。"
        },
        "tansr-event-envelope": {
          "const": "unified-v1",
          "description": "显式协商统一事件包络(方案 §0 D7 ②;RFC §1.5 事件包络仅新 SDK 消费):携此头 = 请求按 EventEnvelope 出帧,服务端以同名响应头回响;不携 = 各族原帧字节不变。"
        }
      },
      "additionalProperties": false,
      "description": "请求侧统一头词表(RFC §1.2;手册 §16.4)。头名按 Node IncomingMessage.headers 小写形登记(HTTP 头名不区分大小写);校验对象为请求头在本词表上的投影。身份字段恒不接受客户端自报(RFC-SDK2-1 §9.1)。"
    },
    "ResponseHeaders": {
      "type": "object",
      "properties": {
        "tansr-contract": {
          "$ref": "#/definitions/Contract"
        },
        "tansr-manifest-revision": {
          "type": "string",
          "pattern": "^[1-9][0-9]*$",
          "description": "= manifest revision(整数的十进制串形)。"
        },
        "tansr-domain": {
          "$)TANSR_SCHEMA",
    R"TANSR_SCHEMA(ref": "#/definitions/Domain"
        },
        "tansr-schema-hash": {
          "$ref": "#/definitions/SchemaHash"
        },
        "tansr-closure-id": {
          "allOf": [
            {
              "$ref": "#/definitions/Digest"
            }
          ],
          "description": "围栏端点 200 与前置复核 412 / 403 / 404 回传当前 closureId(RFC §1.4)。"
        },
        "tansr-event-envelope": {
          "const": "unified-v1",
          "description": "回响请求侧同名协商头(方案 §0 D7 ②):确认本响应(SSE)按 EventEnvelope 出帧;缺席 = 各族原帧。"
        },
        "x-request-id": {
          "$ref": "#/definitions/TraceId"
        },
        "retry-after": {
          "type": "string",
          "pattern": "^[1-9][0-9]*$",
          "description": "秒;503 capacity_exceeded / upstream_unavailable 携带(same-request 须等待)。"
        }
      },
      "required": [
        "tansr-contract",
        "tansr-manifest-revision",
        "tansr-domain",
        "tansr-schema-hash"
      ],
      "additionalProperties": false,
      "description": "响应侧统一头(RFC )TANSR_SCHEMA",
    R"TANSR_SCHEMA(§1.1;手册 §16.4):四个 tansr-* 头恒在(含 SSE 首帧与错误响应);头内恒不含 endUserId、sessionId、令牌、内部文件路径。校验对象为响应头在本词表上的投影。"
    }
  }
}
)TANSR_SCHEMA",
};
static constexpr EmbeddedSchema embedded_schemas[]{
    {"archive-sync-v1", schema_chunks_0, 7},
    {"sdk2-archive-recovery-v1", schema_chunks_1, 13},
    {"sdk2-cache-core-v1", schema_chunks_2, 19},
    {"sdk2-cache-v1", schema_chunks_3, 70},
    {"sdk2-ext-v1", schema_chunks_4, 106},
    {"terminal-observation-v1", schema_chunks_5, 7},
    {"terminal-profile-v1", schema_chunks_6, 11},
    {"terminal-services-v1", schema_chunks_7, 69},
    {"terminal-shell-sandbox-v1", schema_chunks_8, 20},
    {"unified-v1", schema_chunks_9, 80},
};

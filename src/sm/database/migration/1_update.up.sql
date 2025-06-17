BEGIN TRANSACTION;

CREATE TABLE layers_new (
    digest TEXT NOT NULL PRIMARY KEY,
    unpackedDigest TEXT,
    layerId TEXT,
    path TEXT,
    osVersion TEXT,
    version TEXT,
    timestamp TIMESTAMP,
    state INTEGER,
    size INTEGER
);

INSERT INTO layers_new (digest, unpackedDigest, layerId, path, osVersion, version, timestamp, state, size)
SELECT digest, NULL, layerId, path, osVersion, version, timestamp, state, size FROM layers;

DROP TABLE layers;

ALTER TABLE layers_new RENAME TO layers;

COMMIT;

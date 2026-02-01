BEGIN TRANSACTION;

DROP TABLE IF EXISTS instances;
DROP TABLE IF EXISTS items;

-- Recreate old instances
CREATE TABLE instances (
    instanceID TEXT NOT NULL PRIMARY KEY,
    serviceID TEXT,
    subjectID TEXT,
    instance INTEGER,
    uid INTEGER,
    priority INTEGER,
    storagePath TEXT,
    statePath TEXT,
    network BLOB
);


-- Recreate services table
CREATE TABLE services (
    id TEXT NOT NULL,
    version TEXT,
    providerID TEXT,
    imagePath TEXT,
    manifestDigest BLOB,
    state INTEGER,
    timestamp TIMESTAMP,
    size INTEGER,
    GID INTEGER,
    PRIMARY KEY(id, version)
);

-- Recreate layers table
CREATE TABLE layers (
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

ALTER TABLE network DROP COLUMN bridgeIfName;

COMMIT;

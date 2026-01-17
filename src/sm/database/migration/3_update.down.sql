BEGIN TRANSACTION;

-- Recreate old instances table schema
CREATE TABLE IF NOT EXISTS instances_old (
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

-- Migrate data back to old schema
INSERT OR IGNORE INTO instances_old (instanceID, serviceID, subjectID, instance, uid, priority, storagePath, statePath)
SELECT itemID || '_' || subjectID || '_' || instance, itemID, subjectID, instance, uid, priority, storagePath, statePath
FROM instances;

-- Drop new instances table
DROP TABLE IF EXISTS instances;

-- Rename old table
ALTER TABLE instances_old RENAME TO instances;

-- Recreate services table
CREATE TABLE IF NOT EXISTS services (
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
CREATE TABLE IF NOT EXISTS layers (
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

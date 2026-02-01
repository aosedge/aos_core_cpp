BEGIN TRANSACTION;

-- Drop unused tables
DROP TABLE IF EXISTS services;
DROP TABLE IF EXISTS layers;
DROP TABLE IF EXISTS instances;

-- Create new items table
CREATE TABLE items (
    itemID TEXT,
    type TEXT,
    version TEXT,
    manifestDigest TEXT,
    state TEXT,
    timestamp INTEGER,
    PRIMARY KEY(itemID, type, version)
);

-- Create new instances table with updated schema
CREATE TABLE instances (
    itemID TEXT NOT NULL,
    subjectID TEXT NOT NULL,
    instance INTEGER NOT NULL,
    type TEXT NOT NULL DEFAULT 'service',
    preinstalled INTEGER NOT NULL DEFAULT 0,
    version TEXT,
    manifestDigest TEXT,
    runtimeID TEXT,
    ownerID TEXT,
    subjectType TEXT,
    uid INTEGER,
    gid INTEGER,
    priority INTEGER,
    storagePath TEXT,
    statePath TEXT,
    envVars TEXT,
    networkParameters TEXT,
    monitoringParams TEXT,
    PRIMARY KEY(itemID, subjectID, instance, type, preinstalled)
);

ALTER TABLE network ADD COLUMN bridgeIfName TEXT;

COMMIT;

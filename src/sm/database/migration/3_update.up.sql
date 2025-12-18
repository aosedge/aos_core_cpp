BEGIN TRANSACTION;

-- Drop unused tables
DROP TABLE IF EXISTS services;
DROP TABLE IF EXISTS layers;

-- Create new instances table with updated schema
CREATE TABLE IF NOT EXISTS instances_new (
    itemID TEXT NOT NULL,
    subjectID TEXT NOT NULL,
    instance INTEGER NOT NULL,
    type TEXT NOT NULL DEFAULT 'service',
    manifestDigest TEXT,
    runtimeID TEXT,
    subjectType TEXT,
    uid INTEGER,
    gid INTEGER,
    priority INTEGER,
    storagePath TEXT,
    statePath TEXT,
    envVars TEXT,
    networkParameters TEXT,
    monitoringParams TEXT,
    PRIMARY KEY(itemID, subjectID, instance, type)
);

-- Migrate data from old instances table
INSERT OR IGNORE INTO instances_new (itemID, subjectID, instance, type, uid, priority, storagePath, statePath)
SELECT serviceID, subjectID, instance, 'service', uid, priority, storagePath, statePath
FROM instances WHERE serviceID IS NOT NULL;

-- Drop old instances table
DROP TABLE IF EXISTS instances;

-- Rename new table
ALTER TABLE instances_new RENAME TO instances;

COMMIT;

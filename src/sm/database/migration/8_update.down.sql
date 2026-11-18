BEGIN TRANSACTION;

CREATE TABLE instances_new (
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
    monitoringParams TEXT,
    PRIMARY KEY(itemID, subjectID, instance, type, preinstalled)
);

INSERT OR REPLACE INTO instances_new (itemID, subjectID, instance, type, preinstalled, version, manifestDigest,
    runtimeID, ownerID, subjectType, uid, gid, priority, storagePath, statePath, envVars, monitoringParams)
SELECT itemID, subjectID, instance, type, preinstalled, version, manifestDigest, runtimeID, ownerID, subjectType, uid,
    gid, priority, storagePath, statePath, envVars, monitoringParams FROM instances;

DROP TABLE instances;

ALTER TABLE instances_new RENAME TO instances;

COMMIT;

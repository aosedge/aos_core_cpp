BEGIN TRANSACTION;

CREATE TABLE instances_old (
    itemID TEXT NOT NULL,
    version TEXT,
    subjectID TEXT NOT NULL,
    instance INTEGER NOT NULL,
    type TEXT NOT NULL DEFAULT 'service',
    preinstalled INTEGER NOT NULL DEFAULT 0,
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

INSERT OR REPLACE INTO instances_old (
    itemID, version, subjectID, instance, type, preinstalled, manifestDigest, runtimeID, ownerID, subjectType,
    uid, gid, priority, storagePath, statePath, envVars, monitoringParams
)
SELECT
    itemID, version, subjectID, instance, type, preinstalled, manifestDigest, runtimeID, ownerID, subjectType,
    uid, gid, priority, storagePath, statePath, envVars, monitoringParams
FROM instances;

DROP TABLE instances;
ALTER TABLE instances_old RENAME TO instances;

COMMIT;

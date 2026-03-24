BEGIN TRANSACTION;

CREATE TABLE instances_backup (
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

INSERT INTO instances_backup SELECT
    itemID, subjectID, instance, type, preinstalled, version, manifestDigest,
    runtimeID, ownerID, subjectType, uid, gid, priority, storagePath, statePath,
    envVars, networkParameters, monitoringParams
FROM instances;

DROP TABLE instances;
ALTER TABLE instances_backup RENAME TO instances;

COMMIT;

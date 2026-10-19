BEGIN TRANSACTION;

CREATE TABLE IF NOT EXISTS runtimeInstances (
    runtimeID TEXT NOT NULL,
    itemID TEXT NOT NULL,
    subjectID TEXT NOT NULL,
    version TEXT NOT NULL,
    instance INTEGER NOT NULL,
    type TEXT NOT NULL,
    preinstalled INTEGER NOT NULL,
    manifestDigest TEXT NOT NULL,
    state TEXT,
    errorAosCode INTEGER,
    errorSysCode INTEGER,
    errorMessage TEXT,
    PRIMARY KEY (runtimeID, itemID, subjectID, instance, type, version)
);

COMMIT;

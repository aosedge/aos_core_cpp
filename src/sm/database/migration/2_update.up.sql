BEGIN TRANSACTION;

CREATE TABLE IF NOT EXISTS instancenetwork (
    instanceID TEXT NOT NULL,
    networkID TEXT NOT NULL,
    PRIMARY KEY (instanceID, networkID)
);

COMMIT;

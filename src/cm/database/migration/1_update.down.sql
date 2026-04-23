BEGIN TRANSACTION;

CREATE TABLE storagestate_old (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    preinstalled INTEGER,
    storageQuota INTEGER,
    stateQuota INTEGER,
    stateChecksum BLOB,
    PRIMARY KEY(itemID, subjectID, instance, type, preinstalled)
);

INSERT OR REPLACE INTO storagestate_old (
    itemID, subjectID, instance, type, preinstalled, storageQuota, stateQuota, stateChecksum
)
SELECT
    itemID,
    subjectID,
    instance,
    type,
    CASE
        WHEN version = '1' THEN 1
        ELSE 0
    END,
    storageQuota,
    stateQuota,
    stateChecksum
FROM storagestate;

DROP TABLE storagestate;
ALTER TABLE storagestate_old RENAME TO storagestate;

CREATE TABLE networkmanager_instances_old (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    preinstalled INTEGER,
    networkID TEXT,
    nodeID TEXT,
    ip TEXT,
    exposedPorts TEXT,
    dnsServers TEXT,
    PRIMARY KEY(itemID, subjectID, instance, type, preinstalled),
    FOREIGN KEY(networkID) REFERENCES networks(networkID),
    FOREIGN KEY(networkID, nodeID) REFERENCES hosts(networkID, nodeID)
);

INSERT OR REPLACE INTO networkmanager_instances_old (
    itemID, subjectID, instance, type, preinstalled, networkID, nodeID, ip, exposedPorts, dnsServers
)
SELECT
    itemID,
    subjectID,
    instance,
    type,
    CASE
        WHEN version = '1' THEN 1
        ELSE 0
    END,
    networkID,
    nodeID,
    ip,
    exposedPorts,
    dnsServers
FROM networkmanager_instances;

DROP TABLE networkmanager_instances;
ALTER TABLE networkmanager_instances_old RENAME TO networkmanager_instances;

CREATE TABLE pending_connections_old (
    requesterItemID TEXT,
    requesterSubjectID TEXT,
    requesterInstance INTEGER,
    requesterType TEXT,
    requesterPreinstalled INTEGER,
    nodeID TEXT,
    networkID TEXT,
    requesterIP TEXT,
    requesterSubnet TEXT,
    targetItemID TEXT,
    port TEXT,
    protocol TEXT
);

INSERT INTO pending_connections_old (
    requesterItemID, requesterSubjectID, requesterInstance, requesterType, requesterPreinstalled, nodeID, networkID,
    requesterIP, requesterSubnet, targetItemID, port, protocol
)
SELECT
    requesterItemID,
    requesterSubjectID,
    requesterInstance,
    requesterType,
    CASE
        WHEN requesterVersion = '1' THEN 1
        ELSE 0
    END,
    nodeID,
    networkID,
    requesterIP,
    requesterSubnet,
    targetItemID,
    port,
    protocol
FROM pending_connections;

DROP TABLE pending_connections;
ALTER TABLE pending_connections_old RENAME TO pending_connections;

CREATE TABLE launcher_instances_old (
    itemID TEXT,
    subjectID TEXT,
    instance INTEGER,
    type TEXT,
    preinstalled INTEGER,
    manifestDigest TEXT,
    nodeID TEXT,
    prevNodeID TEXT,
    runtimeID TEXT,
    uid INTEGER,
    gid INTEGER,
    timestamp INTEGER,
    state TEXT,
    isUnitSubject INTEGER,
    version TEXT,
    ownerID TEXT,
    subjectType TEXT,
    labels TEXT,
    priority INTEGER,
    disableRebalancing INTEGER,
    PRIMARY KEY(itemID, subjectID, instance, type, preinstalled, version)
);

INSERT OR REPLACE INTO launcher_instances_old (
    itemID, subjectID, instance, type, preinstalled, manifestDigest, nodeID, prevNodeID, runtimeID, uid, gid,
    timestamp, state, isUnitSubject, version, ownerID, subjectType, labels, priority, disableRebalancing
)
SELECT
    itemID,
    subjectID,
    instance,
    type,
    CASE
        WHEN version = '1' THEN 1
        ELSE 0
    END,
    manifestDigest,
    nodeID,
    prevNodeID,
    runtimeID,
    uid,
    gid,
    timestamp,
    state,
    isUnitSubject,
    version,
    ownerID,
    subjectType,
    labels,
    priority,
    disableRebalancing
FROM launcher_instances;

DROP TABLE launcher_instances;
ALTER TABLE launcher_instances_old RENAME TO launcher_instances;

COMMIT;

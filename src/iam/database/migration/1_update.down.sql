UPDATE certificates
SET  
    keyURL = SUBSTR(keyURL, 1, INSTR(keyURL, 'pin-source=') - 1) || 
             'pin-value=' || (SELECT value FROM pins WHERE path = SUBSTR(keyURL, INSTR(keyURL, 'pin-source=') + 11) LIMIT 1),
    certURL = SUBSTR(certURL, 1, INSTR(certURL, 'pin-source=') - 1) || 
              'pin-value=' || (SELECT value FROM pins WHERE path = SUBSTR(certURL, INSTR(certURL, 'pin-source=') + 11) LIMIT 1);

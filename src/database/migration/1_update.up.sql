UPDATE certificates
SET  
    keyURL = SUBSTR(keyURL, 1, INSTR(keyURL, 'pin-value=') - 1) || 
             'pin-source=' || (SELECT path FROM pins WHERE value = SUBSTR(keyURL, INSTR(keyURL, 'pin-value=') + 10) LIMIT 1),
    certURL = SUBSTR(certURL, 1, INSTR(certURL, 'pin-value=') - 1) || 
              'pin-source=' || (SELECT path FROM pins WHERE value = SUBSTR(certURL, INSTR(certURL, 'pin-value=') + 10) LIMIT 1);

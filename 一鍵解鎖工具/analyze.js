
const fs = require('fs');
const path = require('path');

const filePath = path.join(__dirname, 'cad测试_t3_secure.exe');
const buffer = fs.readFileSync(filePath);

// Search for strings
const text = buffer.toString('ascii');
const matches = text.match(/[\x20-\x7E]{4,}/g);
if (matches) {
    matches.forEach(m => {
        if (/limit|count|times|expire|open|lock|protect|trial/i.test(m)) {
            console.log(`String Found: ${m}`);
        }
    });
}

// Search for 9999 (0x270F) in little endian
for (let i = 0; i < buffer.length - 1; i++) {
    if (buffer[i] === 0x0F && buffer[i+1] === 0x27) {
        console.log(`9999 (0x0F27) found at offset: 0x${i.toString(16)}`);
    }
}

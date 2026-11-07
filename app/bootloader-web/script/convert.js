import fs from 'fs';
import path from 'path';

function fileToCArray(outputFilePath, args) {
  try {
    // Read the file as a binary buffer
    let cCode = `// Generated automatically\n`;
    cCode += `#include <stddef.h>\n\n`;

    for (let i = 0; i < args.length - 1; i += 2) {
      const inputFilePath = args[i];
      const arrayName = args[i + 1];
      const buffer = fs.readFileSync(inputFilePath);

      cCode += `const unsigned char ${arrayName}[] = {\n    `;

      const bytesPerLine = 12;
      for (let i = 0; i < buffer.length; i++) {
        // Format each byte as hex (e.g., 0x4F)
        const hex = '0x' + buffer[i].toString(16).padStart(2, '0');
        cCode += hex;

        // Add comma if not the last byte
        if (i < buffer.length - 1) {
          cCode += ', ';
        }

        // Newline every N bytes for readability
        if ((i + 1) % bytesPerLine === 0 && i < buffer.length - 1) {
          cCode += '\n    ';
        }
      }

      cCode += `\n};\n`;
      cCode += `#define ${arrayName}_len_str "${buffer.length}"\n\n`;
    }

    // Write the output C source file
    fs.writeFileSync(outputFilePath, cCode, 'utf8');
    console.log(`Successfully converted to "${outputFilePath}".`);
  } catch (err) {
    console.error('Error converting file:', err);
  }
}

// Example usage:
// node convert.js input.bin output.c my_data_array
const args = process.argv.slice(2);
if (args.length < 3) {
  console.log('Usage: node convert.js <output_c_file> <input_file> <array_name> [<input_file> <array_name>...]');
  process.exit(1);
}

fileToCArray(args[0], args.slice(1));

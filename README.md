# FIT2CSV2FIT_tools
Convert FIT file to CSV and vice versa. 

There are several tools to convert FIT files to CSV and vice versa. I could not find a simple free tool that I can use, so, I wrote my own tool.
The tool is using GARMIN FIT SDK. The published SDK does not include all message type and definitions, so I could not use the provided FIT_*() 
functions in the SDK and instead, I just used the constants and definitioons in the include files and wrote my own file scanning procedure.
The generated CSV file contains mainly numeric values to overcome the missing definitions, and to allow easy conversion back to FIT.
The reson to convert a CSV file back to FIT is to allow changing values in FIT file is required (for instance - update total distance in Totals.fit, in case of upgrading
the GPS unit).

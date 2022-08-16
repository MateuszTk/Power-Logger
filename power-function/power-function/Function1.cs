using System;
using System.IO;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Azure.WebJobs;
using Microsoft.Azure.WebJobs.Extensions.Http;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Logging;
using Newtonsoft.Json;

namespace power_function
{
    public static class Function1
    {
        [FunctionName("Log")]
        public static async Task<IActionResult> Run(
            [HttpTrigger(AuthorizationLevel.Function, "get", "post", Route = null)] HttpRequest req,
            [CosmosDB(
                databaseName: "Measurements",
                collectionName: "Items",
                ConnectionStringSetting = "CosmosDBConnectionString")]IAsyncCollector<dynamic> documentsOut,
            ILogger log)
        {
            log.LogInformation("C# HTTP trigger function processed a request.");

            string data_str = req.Query["d"];

            string requestBody = await new StreamReader(req.Body).ReadToEndAsync();
            dynamic data = JsonConvert.DeserializeObject(requestBody); 
            data_str = data_str ?? data?.d;
            
            string responseMessage;

            const float minValue = -100.0f;
            float current = minValue;
            float voltage = minValue;
            ulong ts = 0;

            /*
             * Incoming data is interpreted this way:
             * -Data set can contain multiple measurements
             * -Each measurement contains:
             *      *Timestamp - marked with the letter 't'  
             *      *Current - marked with the letter 'i'
             *      *Voltage - marked with the letter 'u'
             *  -Syntax of single measurement:
             *      "t<Timestamp>i<Current>u<Voltage>"
             *      Example:
             *      "t1234i88u44"
             *      *'i' and 'u' can be swapped (not with 't' which always must be first)
             *  -If one value is missing (example: "t1234iu88" ['i' value is missing]) the entire measurement is discarded
             *  -If more than one measurement is being sent,
             *   strings of two measurements should be added without the use of additional separators
             *      Example: "t1234i88u44t1235i42u69"
             *
            */

            if (data_str.Length > 0) {
                Console.WriteLine(data_str);
                responseMessage = "OK";

                char[] separators = { 't', 'i', 'u' };
                int start = data_str.IndexOfAny(separators, 0);
                int end = 0;

                //find the end of the number and return the index pointing to the next letter (not digit)
                Func<int, int> Next = starti => {
                    for (int i = starti; i < data_str.Length; i++)
                        if ((data_str[i] < '0' || data_str[i] > '9') && data_str[i] != '.' && data_str[i] != '-')
                            return i;
                    return data_str.Length;
                };

                while (start < data_str.Length - 1) {

                    //fast forward in case of incomplete data (to deal with cases like: "t1234iu88" - value after 'i' missing)
                    while (start < data_str.Length - 1 && (data_str[start + 1] < '0' || data_str[start + 1] > '9') && data_str[start + 1] != '-')
                    {
                        if (data_str[start] == 't')
                        {
                            current = minValue;
                            voltage = minValue;
                            ts = 0;
                        }
                        start++;
                    }
                    if (start >= data_str.Length - 1) break;

                    //find the end of the number
                    end = Next(start + 1);
                    
                    switch (data_str[start])
                    {
                        //every read begins with timestamp - 't'
                        case 't':
                            ts = UInt64.Parse(data_str.Substring(start + 1, end - start - 1));
                            current = minValue;
                            voltage = minValue;
                            break;

                        case 'i':
                            current = float.Parse(data_str.Substring(start + 1, end - start - 1));
                            break;

                        case 'u':
                            voltage = float.Parse(data_str.Substring(start + 1, end - start - 1));
                            break;

                        default:
                            break;
                    }

                    //move to the next value
                    start = end;

                    //if all the variables are read save the measurement
                    if (ts > 0 && current > minValue && voltage > minValue)
                    {
                        log.LogInformation("ts=" + ts + " I=" + current + " U=" + voltage);

                        await documentsOut.AddAsync(new
                        {
                            id = ts.ToString(),
                            U = voltage,
                            I = current
                        });

                        current = minValue;
                        voltage = minValue;
                    }                   
                }
            }
            else
            {
                responseMessage = "Incomplete";
            }

            return new OkObjectResult(responseMessage);
        }
    }
}

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

            string current_str = req.Query["i"];
            string voltage_str = req.Query["u"];

            string requestBody = await new StreamReader(req.Body).ReadToEndAsync();
            dynamic data = JsonConvert.DeserializeObject(requestBody);
            current_str = current_str ?? data?.i;
            voltage_str = voltage_str ?? data?.u;

            string responseMessage;

            int current = 0;
            int voltage = 0;
            if (!string.IsNullOrEmpty(current_str) && !string.IsNullOrEmpty(voltage_str)) {
                responseMessage = "OK";
                current = Int32.Parse(current_str);
                voltage = Int32.Parse(voltage_str);

                log.LogInformation("I=" + current + " U=" + voltage);

                // Send to Cosmos DB
                // Add a JSON document to the output container.
                await documentsOut.AddAsync(new
                {
                    id = DateTime.Now.ToString("yyyyMMddHHmmss"),
                    U = voltage,
                    I = current
                });
            }
            else
            {
                responseMessage = "Incomplete";
            }

            return new OkObjectResult(responseMessage);
        }
    }
}

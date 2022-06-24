const { CosmosClient } = require("@azure/cosmos");

const endpoint = "https://power-cosmos.documents.azure.com:443/";

// read only key
const key = "jBWMx7q9Sy592gt20TUL9dchFNiesR9iPyo5NAxrQyASaAgM2ypCY5fJA1FFkiT0gGej5DwTSbPA5FQSuPjTLA==";

const databaseId = "Measurements";
const containerId = "Items";

//initialize client
const client = new CosmosClient({ endpoint, key });
const database = client.database(databaseId);
const container = database.container(containerId);

export async function queryDB(query_string) {
    console.log(`Querying container: Items`);

    // query to return all items
    const querySpec = {
        query: query_string
    };

    // read all items in the Items container
    const { resources: items } = await container.items
        .query(querySpec)
        .fetchAll();

    return items;
}

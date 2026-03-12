use reqwest;
use serde_json::json;
use std::error::Error;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let client = reqwest::Client::new();
    
    // Credentials from HubbleBLELocker
    let org_id = "YOUR_ORG_ID";
    let token = "YOUR_TOKEN";
    
    let url = format!("https://api.hubble.com/api/v2/org/{}/devices", org_id);

    let nodes = vec![
        ("Node 1 (40MHz)", "b0:cb:d8:8a:06:9c"),
        ("Node 2 (26MHz)", "a4:f0:0f:63:29:64"),
        ("Node 3 (26MHz)", "a4:f0:0f:61:33:dc"),
    ];

    let names: Vec<&str> = nodes.iter().map(|n| n.0).collect();
    // We cannot set MAC/ID on creation per the simplified example, 
    // but we can tag them with the MAC for reference.
    let tags: Vec<serde_json::Value> = nodes.iter().map(|n| {
        json!({
            "mac": n.1,
            "crystal": if n.0.contains("40MHz") { "40MHz" } else { "26MHz" }
        })
    }).collect();

    let payload = json!({
        "n_devices": 3,
        "encryption": "AES-256-CTR",
        "names": names,
        "tags": tags
    });

    println!("Registering 3 Nodes...");
    
    let response = client.post(&url)
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("Authorization", format!("Bearer {}", token))
        .json(&payload)
        .send()
        .await?;

    let status = response.status();
    let body = response.text().await?;
    
    println!("Status: {}", status);
    println!("Response: {}", body);

    Ok(())
}

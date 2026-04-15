# Google Drive Backup Integration Setup

This guide explains how to set up Google Drive cloud backup for your NEXUS Inventory Manager.

## Prerequisites

- Python 3.7+
- Google account with Google Drive access
- Access to Google Cloud Console

## Installation

### 1. Install Python Dependencies

```bash
pip install google-auth-oauthlib google-api-python-client
```

### 2. Create Google Cloud Project

1. Go to [Google Cloud Console](https://console.cloud.google.com)
2. Create a new project
3. Enable the **Google Drive API**:
   - Search for "Google Drive API"
   - Click "Enable"

### 3. Create OAuth 2.0 Credentials

1. Go to **Credentials** in the Cloud Console
2. Click **Create Credentials** → **OAuth client ID**
3. Select **Desktop application**
4. Download the JSON file
5. Open the JSON and note:
   - `client_id` value
   - `client_secret` value

## Configuration in NEXUS

1. Open NEXUS in your browser (http://localhost:8080)
2. Login with your credentials
3. Go to **Backup** tab
4. Scroll down to **☁️ Google Drive Backup** section
5. Click **Configure Credentials**
6. Paste your:
   - **Client ID** from the JSON file
   - **Client Secret** from the JSON file
7. Click **Save Credentials**

## Usage

### Upload Backup to Google Drive

1. Click **Upload Backup** button
2. The system will:
   - Create a local backup of your database
   - Authenticate with Google Drive
   - Upload the backup to a folder named "NEXUS Inventory Backups"

### View Backups on Drive

1. Click **Refresh List** to see all backups stored on Google Drive
2. Backups are organized by creation date
3. Files include timestamp in filename

## Features

- ✅ Automatic backup file creation with timestamp
- ✅ OAuth2 authentication with Google
- ✅ Cloud storage in dedicated folder
- ✅ View backup history with file sizes
- ✅ Easy credential management
- ✅ Works alongside local backups

## Troubleshooting

### "Failed to authenticate"
- Verify Client ID and Client Secret are correct
- Check that Google Drive API is enabled in Cloud Console
- Ensure the credentials are for a Desktop application

### "Backup file not found"
- Check that local backup was created first
- Verify write permissions in the startup directory

### "No backups found on Google Drive"
- Ensure you've uploaded at least one backup
- Check your Google Drive for "NEXUS Inventory Backups" folder
- Verify the credentials are still valid

## Data Privacy

- Backups are encrypted in transit via HTTPS
- Your Client Secret is stored locally in the database
- We recommend using a dedicated service account for production
- Regularly review shared drives and permissions in Google Drive

## Advanced Configuration

The Google Drive integration uses the Python helper script (`gdrive_helper.py`):

```bash
python3 gdrive_helper.py <command> [args]
```

Available commands:
- `auth <client_id> <client_secret>` - Authenticate and get token
- `upload <client_id> <client_secret> <file_path> <token>` - Upload file
- `download <client_id> <client_secret> <file_id> <output_path> <token>` - Download file
- `list <client_id> <client_secret> <token>` - List backups
- `delete <client_id> <client_secret> <file_id> <token>` - Delete backup

## Security Best Practices

1. **Use Service Accounts in Production**
   - Create a service account in Google Cloud
   - Use its credentials instead of personal OAuth

2. **Rotate Credentials Regularly**
   - Update Client Secret periodically
   - Revoke old credentials in Google Cloud Console

3. **Monitor Access**
   - Check Google Drive audit logs
   - Review backup files periodically

4. **Backup the Backup**
   - Keep local copies of critical backups
   - Consider multiple cloud providers

## Support

For issues with:
- **NEXUS Backend**: Check server logs (`./nexus_server` output)
- **Google Drive API**: See [Google Drive API Documentation](https://developers.google.com/drive/api)
- **Python Helper**: Run directly to debug: `python3 gdrive_helper.py`

---

Last Updated: February 2026
NEXUS Inventory Manager v1.0

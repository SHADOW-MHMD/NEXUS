# Google OAuth Setup

This project now supports Google OAuth 2.0 sign-in and HMAC-SHA256 JWTs.

## 1. Create Google OAuth credentials

1. Open Google Cloud Console.
2. Create or select a project.
3. Go to `APIs & Services` -> `OAuth consent screen`.
4. Configure the app name, support email, and authorized domains as needed.
5. Go to `APIs & Services` -> `Credentials`.
6. Create `OAuth client ID`.
7. Choose `Web application`.
8. Add these redirect URIs:
   - `http://localhost:8080/api/auth/oauth/callback`
   - `http://localhost:3000/api/auth/oauth/callback`
   - `https://<CLOUD_RUN_URL>/api/auth/oauth/callback`
9. Save the client and copy the generated `client_id` and `client_secret`.

## 2. Store credentials in environment variables

Set these values in `.env` or your deployment environment:

```env
GOOGLE_CLIENT_ID=your-google-client-id.apps.googleusercontent.com
GOOGLE_CLIENT_SECRET=your-google-client-secret
GOOGLE_REDIRECT_URI=http://localhost:8080/api/auth/oauth/callback
FRONTEND_BASE_URL=http://localhost:8080
JWT_SECRET=generate-at-least-32-random-bytes
JWT_EXPIRATION_SECONDS=86400
```

Production notes:

- Store `JWT_SECRET` in Secret Manager or another secret store.
- Use an HTTPS redirect URI in production.
- Set `FRONTEND_BASE_URL` to the user-facing URL if the frontend is hosted separately.

## 3. Apply the optional PostgreSQL user-column migration

`src/db.c` does not auto-add OAuth columns. Apply the migration manually:

```bash
psql "$POSTGRES_CONNECTION_STRING" -f docs/sql/2026-04-15_add_google_oauth_columns.sql
```

The app still works without these columns, but linking a user to a stable Google subject ID requires them.

## 4. Start the stack

```bash
cp .env.example .env
# edit .env
docker compose up --build
```

## 5. Flow summary

- Frontend calls `POST /api/auth/oauth/start`
- Backend returns a Google authorization URL
- Browser redirects to Google
- Google redirects back to `/api/auth/oauth/callback`
- Backend exchanges the code, creates or updates the user, issues a JWT, and redirects to `/login?token=...`

## 6. Endpoints

- `POST /api/auth/oauth/start`
- `GET /api/auth/oauth/callback`
- `POST /api/auth/refresh`
- `POST /api/auth/logout`
- `POST /api/auth/login` remains available only as a deprecated compatibility path

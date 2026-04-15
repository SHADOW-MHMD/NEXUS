# NEXUS API Documentation

Complete REST API reference for NEXUS Inventory Manager backend.

## Base URL

```
http://localhost:8080/api
```

## Response Format

All responses are JSON. Success responses include a `status` field, error responses include an `error` field.

### Success Response
```json
{
    "status": "success",
    "message": "Operation completed",
    "data": { }
}
```

### Error Response
```json
{
    "error": "Error description"
}
```

## Authentication

All endpoints except login require an `Authorization` header:

```
Authorization: Bearer <token>
```

---

## Endpoints

### Authentication

#### Login
**POST** `/api/auth/login`

Authenticate user and receive session token.

**Parameters (Form)**:
- `username` (string, required) - Username
- `password` (string, required) - Password

**Response**:
```json
{
    "status": "success",
    "token": "eyJhbGciOiJIUzI1NiIs...",
    "user_id": 1,
    "username": "admin",
    "role": "admin"
}
```

**Status Codes**:
- `200` - Login successful
- `401` - Invalid credentials

---

### Items

#### List Items
**GET** `/api/items`

Retrieve all inventory items.

**Query Parameters**:
- `search` (string, optional) - Search by name or SKU
- `category_id` (integer, optional) - Filter by category
- `low_stock` (boolean, optional) - Show only low stock items

**Response**:
```json
[
    {
        "id": 1,
        "sku": "EL-001",
        "name": "Raspberry Pi 4B",
        "quantity": 15,
        "min_quantity": 5,
        "unit_price": 75.00,
        "supplier": "PiShop",
        "location": "Shelf A1",
        "category_id": 1
    }
]
```

#### Create Item
**POST** `/api/items`

Create a new inventory item.

**Parameters (Form)**:
- `sku` (string, required) - Stock Keeping Unit
- `name` (string, required) - Item name
- `description` (string, optional) - Item description
- `category_id` (integer, optional) - Category ID
- `quantity` (integer, required) - Initial quantity
- `min_quantity` (integer, optional) - Minimum stock level
- `unit_price` (decimal, required) - Unit price
- `supplier` (string, optional) - Supplier name
- `location` (string, optional) - Storage location

**Response**:
```json
{
    "status": "success",
    "message": "Item created",
    "item_id": 42
}
```

#### Update Item
**PUT** `/api/items/{id}`

Update an existing item.

**Parameters (Form)**:
- `name` (string) - Item name
- `description` (string) - Item description
- `quantity` (integer) - Current quantity
- `min_quantity` (integer) - Minimum stock level
- `unit_price` (decimal) - Unit price
- `supplier` (string) - Supplier name
- `location` (string) - Storage location

**Response**:
```json
{
    "status": "success",
    "message": "Item updated"
}
```

#### Delete Item
**DELETE** `/api/items/{id}`

Mark an item as inactive (soft delete).

**Response**:
```json
{
    "status": "success",
    "message": "Item deleted"
}
```

#### Get Item Details
**GET** `/api/items/{id}`

Get detailed information for a specific item.

**Response**:
```json
{
    "id": 1,
    "sku": "EL-001",
    "name": "Raspberry Pi 4B 4GB",
    "description": "Single-board computer 4GB RAM",
    "quantity": 15,
    "min_quantity": 5,
    "unit_price": 75.00,
    "supplier": "PiShop",
    "location": "Shelf A1",
    "category_id": 1,
    "created_at": "2024-01-15 10:30:00",
    "updated_at": "2024-01-15 10:30:00"
}
```

---

### Stock Adjustments

#### Adjust Stock
**POST** `/api/items/{id}/adjust`

Adjust item stock and create transaction record.

**Parameters (Form)**:
- `quantity` (integer, required) - Quantity to add/subtract (negative for removal)
- `type` (string, required) - Transaction type (IN, OUT, ADJUST, RETURN)
- `notes` (string, optional) - Transaction notes
- `reference` (string, optional) - Reference (PO, ticket, etc.)

**Response**:
```json
{
    "status": "success",
    "message": "Stock adjusted",
    "new_quantity": 18,
    "transaction_id": 123
}
```

---

### Users

#### List Users
**GET** `/api/users`

Retrieve all users. Requires admin role.

**Query Parameters**:
- `search` (string, optional) - Search by username or full name

**Response**:
```json
[
    {
        "id": 1,
        "username": "admin",
        "role": "admin",
        "full_name": "System Administrator",
        "email": "admin@nexus.local",
        "is_active": 1,
        "created_at": "2024-01-01 00:00:00",
        "last_login": "2024-01-15 14:30:00"
    }
]
```

#### Create User
**POST** `/api/users`

Create a new user. Requires admin role.

**Parameters (Form)**:
- `username` (string, required) - Unique username
- `password` (string, required) - User password
- `role` (string, required) - Role (admin, manager, viewer)
- `full_name` (string, optional) - User's full name
- `email` (string, optional) - Email address

**Response**:
```json
{
    "status": "success",
    "message": "User created",
    "user_id": 42
}
```

#### Update User
**PUT** `/api/users/{id}`

Update user information. Requires admin role.

**Parameters (Form)**:
- `role` (string) - New role
- `full_name` (string) - Full name
- `email` (string) - Email address
- `is_active` (boolean) - Active status

**Response**:
```json
{
    "status": "success",
    "message": "User updated"
}
```

#### Delete User
**DELETE** `/api/users/{id}`

Deactivate a user. Requires admin role.

**Response**:
```json
{
    "status": "success",
    "message": "User deleted"
}
```

#### Change Password
**POST** `/api/users/{id}/password`

Change user password.

**Parameters (Form)**:
- `new_password` (string, required) - New password
- `old_password` (string, required) - Current password (for non-admins)

**Response**:
```json
{
    "status": "success",
    "message": "Password changed"
}
```

---

### Transactions

#### List Transactions
**GET** `/api/transactions`

Retrieve transaction history.

**Query Parameters**:
- `search` (string, optional) - Search in notes or reference
- `item_id` (integer, optional) - Filter by item
- `type` (string, optional) - Filter by type (IN, OUT, ADJUST, ISSUE, RETURN)
- `date_from` (date, optional) - Start date (YYYY-MM-DD)
- `date_to` (date, optional) - End date (YYYY-MM-DD)

**Response**:
```json
[
    {
        "id": 1,
        "item_id": 1,
        "user_id": 1,
        "type": "IN",
        "quantity": 10,
        "unit_price": 75.00,
        "notes": "Purchase order fulfillment",
        "reference": "PO-2024-001",
        "created_at": "2024-01-15 10:30:00"
    }
]
```

---

### Issues & Returns

#### List Issues
**GET** `/api/issues`

Retrieve item issue records.

**Query Parameters**:
- `search` (string, optional) - Search by assignee, item, reference
- `status` (string, optional) - Filter by status (ISSUED, PARTIAL, RETURNED)

**Response**:
```json
[
    {
        "id": 1,
        "item_id": 5,
        "item_name": "Arduino Uno R3",
        "quantity": 5,
        "quantity_returned": 0,
        "assignee": "John Smith",
        "purpose": "Project development",
        "reference": "DEV-001",
        "expected_return_date": "2024-02-15",
        "status": "ISSUED",
        "issued_by": 1,
        "created_at": "2024-01-20 09:00:00"
    }
]
```

#### Create Issue
**POST** `/api/issues`

Create a new item issue (checkout).

**Parameters (Form)**:
- `item_id` (integer, required) - Item to issue
- `quantity` (integer, required) - Quantity to issue
- `assignee` (string, required) - Person/department receiving items
- `purpose` (string, optional) - Reason for issue
- `reference` (string, optional) - Reference (ticket, PO, etc.)
- `expected_return_date` (date, optional) - Expected return date

**Response**:
```json
{
    "status": "success",
    "message": "Issue created",
    "issue_id": 42
}
```

#### Return Items
**POST** `/api/issues/{id}/return`

Return issued items.

**Parameters (Form)**:
- `quantity_returned` (integer, required) - Quantity being returned
- `condition` (string, optional) - Return condition (Good, Damaged, Lost)
- `notes` (string, optional) - Return notes

**Response**:
```json
{
    "status": "success",
    "message": "Items returned",
    "new_status": "RETURNED"
}
```

---

### Categories

#### List Categories
**GET** `/api/categories`

Retrieve all product categories.

**Response**:
```json
[
    {
        "id": 1,
        "name": "Electronics",
        "description": "Electronic components and devices",
        "created_at": "2024-01-01 00:00:00"
    }
]
```

#### Create Category
**POST** `/api/categories`

Create a new product category. Requires manager role.

**Parameters (Form)**:
- `name` (string, required) - Category name
- `description` (string, optional) - Category description

**Response**:
```json
{
    "status": "success",
    "message": "Category created",
    "category_id": 6
}
```

---

### Reports & Analytics

#### Inventory Summary
**GET** `/api/reports/inventory-summary`

Get inventory statistics.

**Response**:
```json
{
    "total_items": 25,
    "total_value": 15000.00,
    "low_stock_count": 3,
    "categories": [
        {
            "name": "Electronics",
            "item_count": 10,
            "total_value": 8000.00
        }
    ]
}
```

#### Stock Levels
**GET** `/api/reports/stock-levels`

Get categorized stock levels.

**Response**:
```json
{
    "adequate": 20,
    "low": 3,
    "critical": 2,
    "items_below_minimum": [
        {
            "id": 1,
            "sku": "EL-001",
            "name": "Raspberry Pi",
            "quantity": 2,
            "min_quantity": 5
        }
    ]
}
```

---

## Status Codes

| Code | Meaning |
|------|---------|
| 200 | OK - Request succeeded |
| 400 | Bad Request - Invalid parameters |
| 401 | Unauthorized - Authentication required |
| 403 | Forbidden - Insufficient permissions |
| 404 | Not Found - Resource doesn't exist |
| 409 | Conflict - SKU already exists |
| 500 | Internal Server Error |

---

## Rate Limiting

- No hard rate limits currently implemented
- Recommended: Implement 100 requests/minute per IP in production

---

## Authentication

The API uses bearer token authentication. Tokens are valid for the session and should be included in the `Authorization` header:

```
GET /api/items
Authorization: Bearer eyJhbGciOiJIUzI1NiIs...
```

Tokens expire when the server restarts. For persistent authentication, consider implementing refresh tokens.

---

## Error Handling

All errors return JSON with an `error` field:

```json
{
    "error": "Item not found"
}
```

Common error messages:
- `"Invalid credentials"` - Login failed
- `"Insufficient stock"` - Can't adjust/issue more than available
- `"Item not found"` - Item ID doesn't exist
- `"Unauthorized"` - No valid token
- `"Forbidden"` - Insufficient permissions for action
- `"SKU already exists"` - Duplicate SKU

---

## Request Examples

### cURL

**Login:**
```bash
curl -X POST http://localhost:8080/api/auth/login \
  -d "username=admin&password=admin123"
```

**Get items:**
```bash
curl -H "Authorization: Bearer TOKEN" \
  http://localhost:8080/api/items
```

**Create item:**
```bash
curl -X POST http://localhost:8080/api/items \
  -H "Authorization: Bearer TOKEN" \
  -d "sku=TEST-001&name=Test%20Item&quantity=10&unit_price=99.99"
```

### JavaScript (Fetch)

```javascript
// Login
const res = await fetch('http://localhost:8080/api/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'username=admin&password=admin123'
});
const data = await res.json();
const token = data.token;

// Get items
const items = await fetch('http://localhost:8080/api/items', {
    headers: { 'Authorization': `Bearer ${token}` }
}).then(r => r.json());
```

---

## Changelog

### v1.0 (Initial Release)
- Basic CRUD operations for items
- User management with roles
- Transaction tracking
- Issue/return workflow
- Category management
- Authentication system

---

**Last Updated**: February 2024
**API Version**: 1.0

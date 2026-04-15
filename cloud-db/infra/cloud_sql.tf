terraform {
  required_version = ">= 1.5.0"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.0"
    }
  }
}

provider "google" {
  project = var.project_id
  region  = var.region
}

variable "project_id" {
  type        = string
  description = "Google Cloud project ID"
}

variable "region" {
  type        = string
  description = "Cloud SQL region"
  default     = "us-central1"
}

variable "environment" {
  type        = string
  description = "Deployment environment: dev or prod"
  default     = "dev"
}

variable "instance_name" {
  type        = string
  description = "Cloud SQL instance name"
  default     = "nexus-db-prod"
}

variable "database_name" {
  type        = string
  description = "Application database name"
  default     = "nexus_db"
}

variable "db_user" {
  type        = string
  description = "Application database user"
  default     = "nexus_app"
}

variable "db_password" {
  type        = string
  description = "Application database password"
  sensitive   = true
}

locals {
  is_prod           = var.environment == "prod"
  tier              = local.is_prod ? "db-n1-standard-2" : "db-f1-micro"
  availability_type = local.is_prod ? "REGIONAL" : "ZONAL"
}

resource "google_sql_database_instance" "nexus" {
  name                = var.instance_name
  region              = var.region
  database_version    = "POSTGRES_15"
  deletion_protection = local.is_prod

  settings {
    tier              = local.tier
    availability_type = local.availability_type
    disk_type         = "PD_SSD"
    disk_size         = 10
    disk_autoresize   = true

    backup_configuration {
      enabled                        = true
      start_time                     = "02:00"
      point_in_time_recovery_enabled = true

      backup_retention_settings {
        retained_backups = 7
        retention_unit   = "COUNT"
      }
    }

    maintenance_window {
      day          = 7
      hour         = 3
      update_track = "stable"
    }

    ip_configuration {
      ipv4_enabled = true
      ssl_mode     = local.is_prod ? "ENCRYPTED_ONLY" : "ALLOW_UNENCRYPTED_AND_ENCRYPTED"
    }

    user_labels = {
      service     = "nexus-db"
      environment = var.environment
      managed-by  = "terraform"
    }
  }
}

resource "google_sql_database" "nexus" {
  name     = var.database_name
  instance = google_sql_database_instance.nexus.name
}

resource "google_sql_user" "nexus_app" {
  name     = var.db_user
  instance = google_sql_database_instance.nexus.name
  password = var.db_password
}

output "instance_name" {
  value = google_sql_database_instance.nexus.name
}

output "connection_string" {
  value       = "postgresql://${var.db_user}:${var.db_password}@${google_sql_database_instance.nexus.public_ip_address}:5432/${var.database_name}"
  sensitive   = true
  description = "Direct TCP connection string for the application"
}

output "proxy_connection_string" {
  value       = "postgresql://${var.db_user}:${var.db_password}@127.0.0.1:5432/${var.database_name}?host=/cloudsql/${var.project_id}:${var.region}:${google_sql_database_instance.nexus.name}"
  sensitive   = true
  description = "Cloud SQL Auth Proxy connection string"
}

#!/bin/bash
#
# Setup environment variables for Docker + Makefile
# This script ensures proper UID/GID and GIT_COMMIT values
#

set -e

echo "Setting up Keystone environment..."

# Get current user's UID and GID
CURRENT_UID=$(id -u)
CURRENT_GID=$(id -g)

# Get git commit hash (short) or use 'latest' if not in git repo
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "latest")

echo "  UID: $CURRENT_UID"
echo "  GID: $CURRENT_GID"
echo "  GIT_COMMIT: $GIT_COMMIT"

# Create or update .env file with proper values
cat > .env << EOF
# .env
# Global environment variables for Docker + Makefile

# Git commit hash (short), fallback to 'latest'
GIT_COMMIT=${GIT_COMMIT}

# Default UID/GID for dev container (use host user's UID/GID)
BUILD_UID=${CURRENT_UID}
BUILD_GID=${CURRENT_GID}
EOF

echo "✓ Environment setup complete!"
echo ""
echo "You can now use:"
echo "  make compile.debug          # Build debug version"
echo "  make docker.shell           # Build and enter dev container"
echo "  make test.debug.asan        # Run tests with ASan"
echo ""
echo "Or use docker-compose directly:"
echo "  docker-compose up -d dev    # Start dev container"
echo "  docker-compose exec dev /bin/bash  # Enter container"

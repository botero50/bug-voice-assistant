#!/bin/bash

# Bug Voice Assistant - Pre-Deployment Setup Script
# Run this before hardware arrives to prepare everything

set -e

PROJECT_DIR="/Users/alejandrobotero/voice assitance"
cd "$PROJECT_DIR"

echo "🎤 BUG VOICE ASSISTANT - PRE-DEPLOYMENT SETUP"
echo "════════════════════════════════════════════════════════"
echo ""

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Step 1: Check configuration files
echo -e "${YELLOW}[1/6] Checking configuration files...${NC}"

if [ ! -f ".env" ]; then
    if [ -f ".env.template" ]; then
        echo "  Creating .env from template..."
        cp .env.template .env
        echo "  ⚠️  Edit .env and add your API keys!"
        echo "     CLAUDE_API_KEY=sk-ant-xxxxx"
        echo "     WIFI_SSID=your-network"
        echo "     WIFI_PASSWORD=your-password"
    else
        echo -e "${RED}  ❌ .env.template not found${NC}"
        exit 1
    fi
else
    echo "  ✅ .env exists"
fi

if [ ! -f "config/device_config.json" ]; then
    if [ ! -d "config" ]; then
        mkdir -p config
    fi
    if [ -f "config/device_config.template.json" ]; then
        echo "  Creating device_config.json from template..."
        cp config/device_config.template.json config/device_config.json
        echo "  ⚠️  Edit config/device_config.json with your settings!"
    else
        echo -e "${RED}  ❌ config/device_config.template.json not found${NC}"
        exit 1
    fi
else
    echo "  ✅ config/device_config.json exists"
fi

echo -e "${GREEN}  ✅ Configuration files ready${NC}"
echo ""

# Step 2: Check Docker installation
echo -e "${YELLOW}[2/6] Checking Docker installation...${NC}"

if ! command -v docker &> /dev/null; then
    echo -e "${RED}  ❌ Docker not found. Install from https://www.docker.com/products/docker-desktop${NC}"
    exit 1
fi

if ! command -v docker-compose &> /dev/null; then
    echo -e "${RED}  ❌ Docker Compose not found${NC}"
    exit 1
fi

echo "  ✅ Docker found: $(docker --version)"
echo "  ✅ Docker Compose found: $(docker-compose --version)"
echo ""

# Step 3: Build Docker services
echo -e "${YELLOW}[3/6] Building Docker services...${NC}"
echo "  This may take 2-5 minutes..."

if docker-compose build > /tmp/docker_build.log 2>&1; then
    echo -e "${GREEN}  ✅ Docker build successful${NC}"
else
    echo -e "${RED}  ❌ Docker build failed. Check /tmp/docker_build.log${NC}"
    cat /tmp/docker_build.log
    exit 1
fi
echo ""

# Step 4: Test Docker services startup
echo -e "${YELLOW}[4/6] Testing Docker services startup...${NC}"

echo "  Starting services..."
docker-compose up -d > /tmp/docker_up.log 2>&1

# Wait for services to start
sleep 5

echo "  Checking service health..."
services_healthy=0
services_total=0

for service in stt llm tts orchestrator; do
    services_total=$((services_total + 1))
    if curl -s http://localhost:500$((services_total))/health > /dev/null 2>&1; then
        echo "    ✅ $service is healthy"
        services_healthy=$((services_healthy + 1))
    else
        echo "    ⚠️  $service not yet responding"
    fi
done

if [ $services_healthy -ge 3 ]; then
    echo -e "${GREEN}  ✅ Most services healthy${NC}"
else
    echo -e "${YELLOW}  ⚠️  Services still starting, please wait...${NC}"
fi

echo "  Stopping services for now..."
docker-compose down > /dev/null 2>&1

echo ""

# Step 5: Check ESP-IDF
echo -e "${YELLOW}[5/6] Checking ESP-IDF installation...${NC}"

if ! command -v idf.py &> /dev/null; then
    echo -e "${YELLOW}  ⚠️  ESP-IDF not found in PATH${NC}"
    echo "    Install from: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/"
    echo "    Or run: brew install esp-idf"
else
    echo "  ✅ ESP-IDF found"
fi

echo ""

# Step 6: Create helpful scripts
echo -e "${YELLOW}[6/6] Creating helper scripts...${NC}"

# Start services script
cat > start_services.sh << 'SCRIPT'
#!/bin/bash
cd "$(dirname "$0")"
echo "Starting Docker services..."
docker-compose up -d
echo "Waiting for services to be healthy..."
sleep 5
echo ""
echo "Service Status:"
docker-compose ps
echo ""
echo "Services are running!"
echo "STT:  http://localhost:5001/health"
echo "LLM:  http://localhost:5002/health"
echo "TTS:  http://localhost:5003/health"
echo "Orchestrator: http://localhost:5000/health"
SCRIPT
chmod +x start_services.sh
echo "  ✅ Created start_services.sh"

# Stop services script
cat > stop_services.sh << 'SCRIPT'
#!/bin/bash
cd "$(dirname "$0")"
echo "Stopping Docker services..."
docker-compose down
echo "✅ Services stopped"
SCRIPT
chmod +x stop_services.sh
echo "  ✅ Created stop_services.sh"

# Test services script
cat > test_services.sh << 'SCRIPT'
#!/bin/bash
echo "Testing Bug Voice Assistant Services..."
echo ""

services=(
    "STT (5001):http://localhost:5001/health"
    "LLM (5002):http://localhost:5002/health"
    "TTS (5003):http://localhost:5003/health"
    "Orchestrator (5000):http://localhost:5000/health"
)

for service in "${services[@]}"; do
    IFS=':' read -r name url <<< "$service"
    if curl -s "$url" > /dev/null 2>&1; then
        echo "✅ $name - healthy"
    else
        echo "❌ $name - not responding"
    fi
done

echo ""
echo "Run './start_services.sh' to start all services"
SCRIPT
chmod +x test_services.sh
echo "  ✅ Created test_services.sh"

echo ""
echo "════════════════════════════════════════════════════════"
echo -e "${GREEN}✅ SETUP COMPLETE!${NC}"
echo ""
echo "📋 Next Steps:"
echo "────────────────────────────────────────────────────────"
echo ""
echo "1️⃣  Edit Configuration Files:"
echo "   nano .env                          (Add your API keys)"
echo "   nano config/device_config.json     (Add location, WiFi)"
echo ""
echo "2️⃣  Prepare MicroSD Card:"
echo "   Follow: DEPLOYMENT_PREP_GUIDE.md (Section 4)"
echo ""
echo "3️⃣  Test Services:"
echo "   ./start_services.sh                (Start Docker)"
echo "   ./test_services.sh                 (Verify health)"
echo "   ./stop_services.sh                 (Stop Docker)"
echo ""
echo "4️⃣  Get API Keys:"
echo "   Claude:  https://console.anthropic.com"
echo "   Gmail:   https://console.cloud.google.com (optional)"
echo "   Outlook: https://portal.azure.com (optional)"
echo ""
echo "5️⃣  Install ESP-IDF (for flashing):"
echo "   brew install esp-idf"
echo "   OR follow: https://docs.espressif.com/projects/esp-idf/"
echo ""
echo "📚 Documentation:"
echo "   DEPLOYMENT_PREP_GUIDE.md  - Complete pre-deployment checklist"
echo "   GPIO_PINOUT_REFERENCE.md  - Hardware pin configuration"
echo "   PHASE_12_HARDWARE_TESTING.md - Device deployment steps"
echo ""
echo "⏰ When hardware arrives (1-2 weeks):"
echo "   1. Flash firmware with: idf.py flash"
echo "   2. Insert MicroSD with models"
echo "   3. Configure WiFi at boot"
echo "   4. Measure battery life"
echo ""
echo "════════════════════════════════════════════════════════"
echo ""

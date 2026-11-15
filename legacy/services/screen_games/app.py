from flask import Flask, jsonify, request
import random
from datetime import datetime

app = Flask(__name__)

# Game sessions
game_sessions = {}

# Tic-Tac-Toe game logic
class TicTacToe:
    def __init__(self):
        self.board = [' ' for _ in range(9)]
        self.current_player = 'X'  # User
        self.ai_player = 'O'
        self.game_over = False
        self.winner = None

    def make_move(self, position, player):
        if self.board[position] == ' ':
            self.board[position] = player
            return True
        return False

    def get_available_moves(self):
        return [i for i, spot in enumerate(self.board) if spot == ' ']

    def check_winner(self, player):
        win_combinations = [
            [0, 1, 2], [3, 4, 5], [6, 7, 8],  # Rows
            [0, 3, 6], [1, 4, 7], [2, 5, 8],  # Columns
            [0, 4, 8], [2, 4, 6]              # Diagonals
        ]
        for combo in win_combinations:
            if all(self.board[i] == player for i in combo):
                return True
        return False

    def ai_move(self):
        """Simple AI: prioritize center, corners, edges"""
        available = self.get_available_moves()
        if not available:
            return None

        # Prioritize center
        if 4 in available:
            return 4
        # Prioritize corners
        corners = [0, 2, 6, 8]
        corner_moves = [c for c in corners if c in available]
        if corner_moves:
            return random.choice(corner_moves)
        # Take any available
        return random.choice(available)

    def get_state(self):
        return {
            "board": self.board,
            "current_player": self.current_player,
            "available_moves": self.get_available_moves(),
            "game_over": self.game_over,
            "winner": self.winner
        }

# 2048 Game Logic (simplified)
class Game2048:
    def __init__(self):
        self.grid = [[0 for _ in range(4)] for _ in range(4)]
        self.score = 0
        self.game_over = False
        self.won = False
        self.add_new_tile()
        self.add_new_tile()

    def add_new_tile(self):
        available = [(i, j) for i in range(4) for j in range(4) if self.grid[i][j] == 0]
        if available:
            i, j = random.choice(available)
            self.grid[i][j] = 4 if random.random() < 0.1 else 2

    def move(self, direction):
        """Move tiles in direction: up, down, left, right"""
        if direction == "left":
            self.grid = [self.merge_line(row) for row in self.grid]
        elif direction == "right":
            self.grid = [self.merge_line(row[::-1])[::-1] for row in self.grid]
        elif direction == "up":
            self.grid = [[self.grid[i][j] for i in range(4)] for j in range(4)]
            self.grid = [self.merge_line(row) for row in self.grid]
            self.grid = [[self.grid[i][j] for i in range(4)] for j in range(4)]
        elif direction == "down":
            self.grid = [[self.grid[i][j] for i in range(4)] for j in range(4)]
            self.grid = [self.merge_line(row[::-1])[::-1] for row in self.grid]
            self.grid = [[self.grid[i][j] for i in range(4)] for j in range(4)]

        self.add_new_tile()
        self.check_game_state()

    def merge_line(self, line):
        """Merge tiles in a line (2048 logic)"""
        # Remove zeros
        line = [x for x in line if x != 0]
        # Merge adjacent pairs
        merged = []
        i = 0
        while i < len(line):
            if i + 1 < len(line) and line[i] == line[i + 1]:
                merged.append(line[i] * 2)
                self.score += line[i] * 2
                i += 2
            else:
                merged.append(line[i])
                i += 1
        # Pad with zeros
        while len(merged) < 4:
            merged.append(0)
        return merged

    def check_game_state(self):
        # Check for 2048
        for row in self.grid:
            if 2048 in row:
                self.won = True

        # Check for available moves
        has_moves = any(0 in row for row in self.grid)
        if not has_moves:
            # Check if any merges are possible
            for i in range(4):
                for j in range(4):
                    if j + 1 < 4 and self.grid[i][j] == self.grid[i][j + 1]:
                        return
                    if i + 1 < 4 and self.grid[i][j] == self.grid[i + 1][j]:
                        return
            self.game_over = True

    def get_state(self):
        return {
            "grid": self.grid,
            "score": self.score,
            "game_over": self.game_over,
            "won": self.won
        }

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "screen_games"}), 200

# Tic-Tac-Toe Endpoints
@app.route('/tictactoe/start', methods=['POST'])
def start_tictactoe():
    """Start a new Tic-Tac-Toe game"""
    try:
        session_id = f"ttt_{datetime.now().timestamp()}"
        game = TicTacToe()
        game_sessions[session_id] = {"type": "tictactoe", "game": game}

        return jsonify({
            "session_id": session_id,
            "game_state": game.get_state(),
            "message": "Tic-Tac-Toe started. X goes first (you). Positions 0-8.",
            "board_visual": format_tictactoe_board(game.board),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/tictactoe/<session_id>/move', methods=['POST'])
def tictactoe_move(session_id):
    """Make a move in Tic-Tac-Toe"""
    try:
        if session_id not in game_sessions or game_sessions[session_id]["type"] != "tictactoe":
            return jsonify({"error": "Game session not found"}), 404

        data = request.get_json() or {}
        position = data.get("position")

        if not isinstance(position, int) or position < 0 or position > 8:
            return jsonify({"error": "Invalid position (0-8)"}), 400

        game = game_sessions[session_id]["game"]

        # Player move
        if not game.make_move(position, 'X'):
            return jsonify({"error": "Position already taken"}), 400

        if game.check_winner('X'):
            game.game_over = True
            game.winner = "X"
            return jsonify({
                "game_state": game.get_state(),
                "message": "¡Ganaste!",
                "board_visual": format_tictactoe_board(game.board)
            }), 200

        # AI move
        ai_pos = game.ai_move()
        if ai_pos is not None:
            game.make_move(ai_pos, 'O')

            if game.check_winner('O'):
                game.game_over = True
                game.winner = "O"
                return jsonify({
                    "game_state": game.get_state(),
                    "message": "AI ganó",
                    "board_visual": format_tictactoe_board(game.board),
                    "ai_move": ai_pos
                }), 200

        # Check for draw
        if not game.get_available_moves():
            game.game_over = True
            game.winner = "Draw"

        return jsonify({
            "game_state": game.get_state(),
            "board_visual": format_tictactoe_board(game.board),
            "ai_move": ai_pos,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

def format_tictactoe_board(board):
    """Format board for display"""
    lines = [
        f" {board[0]} | {board[1]} | {board[2]} ",
        "-----------",
        f" {board[3]} | {board[4]} | {board[5]} ",
        "-----------",
        f" {board[6]} | {board[7]} | {board[8]} "
    ]
    return "\n".join(lines)

# 2048 Endpoints
@app.route('/2048/start', methods=['POST'])
def start_2048():
    """Start a new 2048 game"""
    try:
        session_id = f"2048_{datetime.now().timestamp()}"
        game = Game2048()
        game_sessions[session_id] = {"type": "2048", "game": game}

        return jsonify({
            "session_id": session_id,
            "game_state": game.get_state(),
            "message": "2048 started. Merge tiles to reach 2048!",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/2048/<session_id>/move', methods=['POST'])
def move_2048(session_id):
    """Make a move in 2048"""
    try:
        if session_id not in game_sessions or game_sessions[session_id]["type"] != "2048":
            return jsonify({"error": "Game session not found"}), 404

        data = request.get_json() or {}
        direction = data.get("direction")  # up, down, left, right

        if direction not in ["up", "down", "left", "right"]:
            return jsonify({"error": "Invalid direction"}), 400

        game = game_sessions[session_id]["game"]
        game.move(direction)

        return jsonify({
            "game_state": game.get_state(),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/stats', methods=['GET'])
def get_stats():
    """Get game statistics"""
    tictactoe_games = sum(1 for s in game_sessions.values() if s["type"] == "tictactoe")
    game_2048 = sum(1 for s in game_sessions.values() if s["type"] == "2048")

    return jsonify({
        "tictactoe_games": tictactoe_games,
        "2048_games": game_2048,
        "total_sessions": len(game_sessions),
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    print("🎮 Screen Games Service Starting...")
    print("Supported games:")
    print("  - Tic-Tac-Toe (AI opponent)")
    print("  - 2048 (tile merging)")
    app.run(host='0.0.0.0', port=5117, debug=False)

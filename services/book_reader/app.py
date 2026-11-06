from flask import Flask, jsonify, request
import os
from datetime import datetime

app = Flask(__name__)

# Mock book library (in production, load from EPUB/PDF files)
BOOKS = {
    "the-hobbit": {
        "title": "The Hobbit",
        "author": "J.R.R. Tolkien",
        "chapters": [
            {"number": 1, "title": "An Unexpected Party", "pages": 24},
            {"number": 2, "title": "Roast Mutton", "pages": 20},
            {"number": 3, "title": "A Short Rest", "pages": 18},
            {"number": 4, "title": "Over Hill and Under Hill", "pages": 22},
            {"number": 5, "title": "Riddles in the Dark", "pages": 25},
        ],
        "total_pages": 310,
        "language": "English"
    },
    "1984": {
        "title": "1984",
        "author": "George Orwell",
        "chapters": [
            {"number": 1, "title": "It was a bright cold day", "pages": 30},
            {"number": 2, "title": "The Party", "pages": 28},
            {"number": 3, "title": "War is Peace", "pages": 25},
        ],
        "total_pages": 328,
        "language": "English"
    }
}

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "book_reader"}), 200

@app.route('/books', methods=['GET'])
def get_books():
    """Get list of available books"""
    books = []
    for book_id, book_data in BOOKS.items():
        books.append({
            "id": book_id,
            "title": book_data["title"],
            "author": book_data["author"],
            "total_pages": book_data["total_pages"],
            "chapter_count": len(book_data["chapters"])
        })

    return jsonify({"books": books, "timestamp": datetime.now().isoformat()}), 200

@app.route('/book/<book_id>', methods=['GET'])
def get_book(book_id):
    """Get book details"""
    if book_id not in BOOKS:
        return jsonify({"error": "Book not found"}), 404

    book = BOOKS[book_id]
    return jsonify({
        "id": book_id,
        "title": book["title"],
        "author": book["author"],
        "chapters": book["chapters"],
        "total_pages": book["total_pages"],
        "language": book["language"],
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/book/<book_id>/chapter/<int:chapter_num>', methods=['GET'])
def get_chapter(book_id, chapter_num):
    """Get specific chapter content"""
    if book_id not in BOOKS:
        return jsonify({"error": "Book not found"}), 404

    book = BOOKS[book_id]
    chapters = {ch["number"]: ch for ch in book["chapters"]}

    if chapter_num not in chapters:
        return jsonify({"error": "Chapter not found"}), 404

    chapter = chapters[chapter_num]

    # Mock chapter content
    mock_content = f"""Chapter {chapter["number"]}: {chapter["title"]}

In a hole in the ground there lived a hobbit. Not a nasty, dirty, wet hole, filled with the ends of worms and an oozy smell, nor yet a dry, bare, sandy hole with nothing in it to sit down on or to eat: it was a hobbit-hole, and that means comfort.

It had a perfectly round door like a porthole, painted green, with a shiny yellow brass knob in the exact center. The door opened on to a tube-shaped hall like a tunnel: a very comfortable tunnel without smoke, with paneled walls, and floors tiled and carpeted, provided with a chair, and lots of pegs for hats and coats—the hobbit was fond of visitors.

The tunnel wound on and on, going fairly but not quite straight into the side of the hill—The Hill, as all the people for many miles round called it—and many little round doors opened out of it, first on one side and then on another. None of these doors had bells on them, as in the hobbit-hole of Bag End was a hobbit-hole, and that means comfort.
"""

    return jsonify({
        "book_id": book_id,
        "book_title": book["title"],
        "chapter": chapter_num,
        "chapter_title": chapter["title"],
        "content": mock_content,
        "page_count": chapter["pages"],
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/book/<book_id>/progress', methods=['POST'])
def update_progress(book_id):
    """Update reading progress"""
    try:
        if book_id not in BOOKS:
            return jsonify({"error": "Book not found"}), 404

        data = request.get_json() or {}
        chapter = data.get("chapter", 1)
        page = data.get("page", 1)

        book = BOOKS[book_id]
        total_pages = book["total_pages"]

        progress = (page / total_pages) * 100

        return jsonify({
            "book_id": book_id,
            "chapter": chapter,
            "page": page,
            "total_pages": total_pages,
            "progress_percent": progress,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/book/<book_id>/next-chapter', methods=['POST'])
def next_chapter(book_id):
    """Advance to next chapter"""
    try:
        if book_id not in BOOKS:
            return jsonify({"error": "Book not found"}), 404

        data = request.get_json() or {}
        current_chapter = data.get("current_chapter", 1)

        book = BOOKS[book_id]
        next_ch_num = current_chapter + 1

        chapters = {ch["number"]: ch for ch in book["chapters"]}
        if next_ch_num not in chapters:
            return jsonify({"error": "No more chapters", "end_of_book": True}), 400

        next_chapter_data = chapters[next_ch_num]

        return jsonify({
            "book_id": book_id,
            "chapter": next_ch_num,
            "chapter_title": next_chapter_data["title"],
            "page": 1,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    print("📖 Book Reader Service Starting...")
    print(f"📚 Available books: {len(BOOKS)}")
    app.run(host='0.0.0.0', port=5113, debug=False)

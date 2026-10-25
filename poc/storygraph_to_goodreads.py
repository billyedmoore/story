#!/usr/bin/env python3
"""Convert a StoryGraph library export CSV into Goodreads import/export CSV format."""
import argparse
import csv
import json
import sys

GOODREADS_COLUMNS = [
    "Book Id", "Title", "Author", "Author l-f", "Additional Authors", "ISBN", "ISBN13",
    "My Rating", "Average Rating", "Publisher", "Binding", "Number of Pages",
    "Year Published", "Original Publication Year", "Date Read", "Date Added",
    "Bookshelves", "Bookshelves with positions", "Exclusive Shelf", "My Review",
    "Spoiler", "Private Notes", "Read Count", "Owned Copies",
]

BINDINGS = {
    "hardcover": "Hardcover",
    "paperback": "Paperback",
    "digital": "Kindle Edition",
    "audio": "Audiobook",
}

# StoryGraph status -> (Goodreads exclusive shelf, extra custom shelf)
STATUSES = {
    "read": ("read", ""),
    "currently-reading": ("currently-reading", ""),
    "to-read": ("to-read", ""),
    "did-not-finish": ("read", "did-not-finish"),
}


def author_last_first(name):
    parts = name.split()
    return f"{parts[-1]}, {' '.join(parts[:-1])}" if len(parts) > 1 else name


def split_isbn(value):
    value = value.strip().replace("-", "")
    if len(value) == 13 and value.isdigit():
        return "", value
    if len(value) == 10:
        return value, ""
    return "", ""  # non-ISBN UIDs (e.g. ASINs) have no Goodreads equivalent


OVERRIDES = {}  # title prefix -> ISBN-13, or {"isbn", "format", "exclude"}; loaded from --overrides


def apply_override(row):
    """Return the row with any matching override applied, or None if it is excluded."""
    for prefix, value in OVERRIDES.items():
        if row["Title"].startswith(prefix):
            fix = value if isinstance(value, dict) else {"isbn": value}
            if fix.get("exclude"):
                return None
            if "isbn" in fix:
                row = dict(row, **{"ISBN/UID": fix["isbn"]})
            if "format" in fix:
                row = dict(row, Format=fix["format"])
    return row


def convert_row(row, book_id):
    authors = [a.strip() for a in row.get("Authors", "").split(",") if a.strip()]
    extra = [a.strip() for a in row.get("Contributors", "").split(",") if a.strip()]
    isbn, isbn13 = split_isbn(row.get("ISBN/UID", ""))
    shelf, custom = STATUSES.get(row.get("Read Status", "").strip().lower(), ("to-read", ""))
    shelves = [s for s in [custom, *(t.strip() for t in row.get("Tags", "").split(","))] if s]
    rating = row.get("Star Rating", "").strip()
    # Goodreads ratings are whole stars; StoryGraph allows quarter stars.
    my_rating = str(round(float(rating))) if rating else "0"
    return {
        "Book Id": book_id,
        "Title": row.get("Title", ""),
        "Author": authors[0] if authors else "",
        "Author l-f": author_last_first(authors[0]) if authors else "",
        "Additional Authors": ", ".join(authors[1:] + extra),
        "ISBN": f'="{isbn}"' if isbn else '=""',
        "ISBN13": f'="{isbn13}"' if isbn13 else '=""',
        "My Rating": my_rating,
        "Average Rating": "",
        "Publisher": "",
        "Binding": BINDINGS.get(row.get("Format", "").strip().lower(), ""),
        "Number of Pages": "",
        "Year Published": "",
        "Original Publication Year": "",
        "Date Read": row.get("Last Date Read", "") if shelf == "read" and custom == "" else "",
        "Date Added": row.get("Date Added", ""),
        "Bookshelves": ", ".join(shelves),
        "Bookshelves with positions": "",
        "Exclusive Shelf": shelf,
        "My Review": row.get("Review", ""),
        "Spoiler": "",
        "Private Notes": "",
        "Read Count": row.get("Read Count", "") or "0",
        "Owned Copies": "1" if row.get("Owned?", "").strip().lower() == "yes" else "0",
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="StoryGraph export CSV")
    ap.add_argument("-o", "--output", default="goodreads_library_export.csv")
    ap.add_argument("--overrides", metavar="JSON",
                    help='JSON object of title prefix -> ISBN-13 or {"isbn", "format", "exclude": true}')
    args = ap.parse_args()
    if args.overrides:
        with open(args.overrides, encoding="utf-8") as f:
            OVERRIDES.update(json.load(f))

    with open(args.input, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows or "Title" not in rows[0] or "Read Status" not in rows[0]:
        sys.exit("Input doesn't look like a StoryGraph export (missing Title / Read Status).")
    rows = [r for r in map(apply_override, rows) if r is not None]

    with open(args.output, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=GOODREADS_COLUMNS)
        w.writeheader()
        for i, row in enumerate(rows, start=1):
            w.writerow(convert_row(row, i))
    print(f"Converted {len(rows)} books -> {args.output}")


if __name__ == "__main__":
    main()

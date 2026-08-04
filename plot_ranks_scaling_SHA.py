import csv
import matplotlib.pyplot as plt
import argparse

CSV_PATH = "rank_scaling_sha256.csv"
OUT_PATH = "rank_scaling.png"
OUT_PATH_NO_SHA = "rank_scaling_no_SHAhost.png"

FILTER_RANKS = [1, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40]

SERIES = [
    ("timeUPMEM_asyncTrans", "ASYNC Transfer", "green"),
    ("timeUPMEM_asyncExec", "ASYNC Execution", "gold"),
    ("timeSHAHost", "SHA Host", "blue"),
]


# CSV Reader
def read_csv(path):
    n_ranks, data_size = [], []
    series_values = {key: [] for key, _, _ in SERIES}
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        reader.fieldnames = [name.strip() for name in reader.fieldnames]
        for row in reader:
            row = {k.strip(): v.strip() for k, v in row.items()}
            n_ranks.append(int(row["nRanks"]))
            data_size.append(float(row["dataSize"]))
            for key in series_values:
                series_values[key].append(float(row[key]))
    return n_ranks, data_size, series_values


def filter_by_ranks(n_ranks, data_size, series_values, allowed_ranks):
    keys = list(series_values.keys())
    filtered = [
        (nr, ds, *[series_values[k][i] for k in keys])
        for i, (nr, ds) in enumerate(zip(n_ranks, data_size))
        if nr in allowed_ranks
    ]
    if not filtered:
        return [], [], {k: [] for k in keys}
    unzipped = list(zip(*filtered))
    n_ranks_f, data_size_f = list(unzipped[0]), list(unzipped[1])
    series_values_f = {k: list(unzipped[2 + idx]) for idx, k in enumerate(keys)}
    return n_ranks_f, data_size_f, series_values_f


def make_plot(labels, series_values_s, series_list, out_path):
    x = range(len(labels))
    plt.figure(figsize=(10, 6))
    for key, label, color in series_list:
        if out_path == OUT_PATH_NO_SHA:
            plt.plot(x, [v * 1000 for v in series_values_s[key]], marker="o", color=color, label=label)
        else:
            plt.plot(x, series_values_s[key], marker="o", color=color, label=label)
    plt.xticks(list(x), labels, rotation=45, ha="right")
    if out_path == OUT_PATH_NO_SHA:
        plt.ylabel("Time (ms)")
    else:
        plt.ylabel("Time (s)")
    plt.xlabel("MB (# ranks)")
    plt.legend()
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    plt.savefig(out_path, dpi=150)
    plt.close()


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate scaling plots from rank_scaling_sha256.csv."
    )
    parser.add_argument(
        "--filter-by-ranks",
        action="store_true",
        help="Plot only the ranks specified in FILTER_RANKS."
    )
    return parser.parse_args()


def main():
    args = parse_args()
    n_ranks, data_size, series_values = read_csv(CSV_PATH)
    if args.filter_by_ranks:
        n_ranks, data_size, series_values = filter_by_ranks(
            n_ranks,
            data_size,
            series_values,
            FILTER_RANKS,
        )
    series_values_s = {
        key: [v / 1000 for v in values] for key, values in series_values.items()
    }
    labels = [f"{int(ds)} MB ({nr} ranks)" for ds, nr in zip(data_size, n_ranks)]
    make_plot(labels, series_values_s, SERIES, OUT_PATH)
    series_no_sha = [s for s in SERIES if s[0] != "timeSHAHost"]
    make_plot(labels, series_values_s, series_no_sha, OUT_PATH_NO_SHA)


if __name__ == "__main__":
    main()
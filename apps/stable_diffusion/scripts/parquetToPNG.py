import pyarrow.parquet as pq


def main():
    index  = 0
    table = pq.read_table('data.parquet');
    for img in table['image']:
        b = img['bytes'].as_py();
        with open(f'{index}.png', 'wb') as f:
            f.write(b)
            index+=1


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
import gzip
import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.metrics import accuracy_score, mean_absolute_error, confusion_matrix, classification_report

def compute_expanded_features(df):
    slots = df[[f'slot{i}' for i in range(32)]].values.astype(np.float64)  # (N, 32)
    eps = 1e-9
    N = len(df)

    total_energy = np.sum(slots, axis=1)
    mean_energy = total_energy / 32.0 + eps
    max_energy = np.max(slots, axis=1)
    peak_slot = np.argmax(slots, axis=1)
    peak_to_mean = max_energy / mean_energy

    # 1. Per-quarter-frame energy ratios
    q1 = np.sum(slots[:, :8], axis=1) + eps
    q2 = np.sum(slots[:, 8:16], axis=1) + eps
    q3 = np.sum(slots[:, 16:24], axis=1) + eps
    q4 = np.sum(slots[:, 24:], axis=1) + eps

    q1_frac = q1 / (total_energy + eps)
    q2_frac = q2 / (total_energy + eps)
    q3_frac = q3 / (total_energy + eps)
    q4_frac = q4 / (total_energy + eps)

    # 2. Half-frame energy ratio (H2 / H1)
    h1 = q1 + q2
    h2 = q3 + q4
    h2_h1_ratio = h2 / h1

    # 3. Late-slot energy ratios (slots 24..31 and slots 28..31)
    late_8_frac = np.sum(slots[:, 24:], axis=1) / (total_energy + eps)
    late_4_frac = np.sum(slots[:, 28:], axis=1) / (total_energy + eps)

    # 4. Energy-weighted slot centroid (spectral/temporal centroid)
    slot_indices = np.arange(32, dtype=np.float64)
    centroid = np.sum(slots * slot_indices, axis=1) / (total_energy + eps)

    # 5. Count of distinct local energy peaks > 0.5 * max_energy
    num_peaks_05 = np.zeros(N, dtype=int)
    for i in range(N):
        s = slots[i]
        p_thresh = 0.5 * max_energy[i]
        p_count = 0
        for k in range(32):
            left = s[k-1] if k > 0 else 0.0
            right = s[k+1] if k < 31 else 0.0
            if s[k] >= left and s[k] >= right and s[k] >= p_thresh:
                p_count += 1
        num_peaks_05[i] = p_count

    # 6. Maximum onset ratio
    ratios = np.zeros((N, 32))
    for k in range(1, 32):
        prev_mean = np.mean(slots[:, max(0, k-4):k], axis=1) + eps
        ratios[:, k] = slots[:, k] / prev_mean

    max_onset_ratio = np.max(ratios, axis=1)
    max_onset_slot = np.argmax(ratios, axis=1)

    feats = pd.DataFrame({
        'total_energy': total_energy,
        'log_total_energy': np.log10(total_energy + 1.0),
        'mean_energy': mean_energy,
        'max_energy': max_energy,
        'peak_slot': peak_slot,
        'peak_to_mean': peak_to_mean,
        'max_onset_ratio': max_onset_ratio,
        'max_onset_slot': max_onset_slot,
        'h2_h1_ratio': h2_h1_ratio,
        'q1_frac': q1_frac,
        'q2_frac': q2_frac,
        'q3_frac': q3_frac,
        'q4_frac': q4_frac,
        'late_8_frac': late_8_frac,
        'late_4_frac': late_4_frac,
        'centroid': centroid,
        'num_peaks_05': num_peaks_05
    })
    return feats, slots

def predict_sbr_grid_tree_separate(dt_class, dt_nenv, feats_row):
    p_c = dt_class.predict(feats_row)[0]
    p_ne = dt_nenv.predict(feats_row)[0]
    o_slot = feats_row['max_onset_slot'].values[0]

    if p_c == 0: # FIXFIX
        p_tenv = [0, 8, 16] if p_ne == 2 else [0, 16]
        p_fres = [1, 1] if p_ne == 2 else [1]
    elif p_c == 1: # FIXVAR
        b1 = int(o_slot)
        b2 = min(15, b1 + 4)
        b3 = min(16, b2 + 4)
        if p_ne == 4:
            p_tenv = [0, b1, b2, b3, 18]
            p_fres = [1, 0, 0, 1]
        elif p_ne == 3:
            p_tenv = [0, b1, b2, 16]
            p_fres = [1, 0, 1]
        else:
            p_tenv = [0, b1, 16]
            p_fres = [1, 1]
    elif p_c == 2: # VARFIX
        t0 = max(0, min(3, int(o_slot)))
        p_tenv = [t0, 7, 16]
        p_fres = [1, 1]
    else: # VARVAR
        b0 = max(0, min(2, int(o_slot) - 2))
        b1 = int(o_slot)
        b2 = min(12, b1 + 4)
        b3 = min(15, b2 + 4)
        p_tenv = [b0, b1, b2, b3, 18]
        p_fres = [1, 0, 0, 1]

    return p_c, p_ne, p_tenv, p_fres

def main():
    df64 = pd.read_csv('probe/dataset/sbr_grid_dataset_64k.csv.gz')
    feats, slots = compute_expanded_features(df64)

    unique_clips = sorted(df64['clip'].unique())
    np.random.seed(42)
    train_clips = set(np.random.choice(unique_clips, size=int(len(unique_clips)*0.8), replace=False))
    test_clips = set(unique_clips) - train_clips

    train_mask = df64['clip'].isin(train_clips)
    test_mask = df64['clip'].isin(test_clips)

    X_train = feats[train_mask]
    X_test = feats[test_mask]

    y_train_c = df64.loc[train_mask, 'ref_frameClass']
    y_test_c = df64.loc[test_mask, 'ref_frameClass']

    y_train_ne = df64.loc[train_mask, 'ref_numEnvelopes']
    y_test_ne = df64.loc[test_mask, 'ref_numEnvelopes']

    print("=== Training Separate Decision Trees (Depths 3-8) ===")
    for depth in range(3, 9):
        dt_c = DecisionTreeClassifier(max_depth=depth, random_state=42)
        dt_c.fit(X_train, y_train_c)

        dt_ne = DecisionTreeClassifier(max_depth=depth, random_state=42)
        dt_ne.fit(X_train, y_train_ne)

        te_acc_c = accuracy_score(y_test_c, dt_c.predict(X_test))
        te_acc_ne = accuracy_score(y_test_ne, dt_ne.predict(X_test))

        # Strict Full Match
        sub_test_df = df64[test_mask].reset_index(drop=True)
        sub_test_feats = feats[test_mask].reset_index(drop=True)

        full_matches = 0
        for i in range(len(sub_test_df)):
            ref_c = sub_test_df.at[i, 'ref_frameClass']
            ref_ne = sub_test_df.at[i, 'ref_numEnvelopes']

            p_c, p_ne, p_tenv, _ = predict_sbr_grid_tree_separate(dt_c, dt_ne, sub_test_feats.iloc[[i]])

            if p_c == ref_c and p_ne == ref_ne:
                ref_tenv = [sub_test_df.at[i, f'ref_tEnv{j}'] for j in range(ref_ne + 1)]
                borders_ok = True
                for j in range(min(len(p_tenv), len(ref_tenv))):
                    if ref_tenv[j] != -1:
                        if abs(p_tenv[j] - ref_tenv[j]) > 2:
                            borders_ok = False
                            break
                if borders_ok:
                    full_matches += 1

        full_match_rate = full_matches / len(sub_test_df)
        print(f"Depth {depth:2d} | frameClass Test Acc: {te_acc_c:.4f} | numEnvelopes Test Acc: {te_acc_ne:.4f} | Strict Full-Match Rate: {full_match_rate*100:.2f}%")

if __name__ == '__main__':
    main()

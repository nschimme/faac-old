#!/usr/bin/env python3
import gzip
import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.metrics import accuracy_score, mean_absolute_error, confusion_matrix, classification_report

def compute_expanded_features_v3(df):
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

    # 3. Late-slot energy ratios
    late_8_frac = np.sum(slots[:, 24:], axis=1) / (total_energy + eps)
    late_4_frac = np.sum(slots[:, 28:], axis=1) / (total_energy + eps)

    # 4. Energy-weighted slot centroid
    slot_indices = np.arange(32, dtype=np.float64)
    centroid = np.sum(slots * slot_indices, axis=1) / (total_energy + eps)

    # 5. Local energy peaks count
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

    # Cross-frame columns from v2 dataset
    prev_ref_frameClass = df['prev_ref_frameClass']
    prev_ref_numEnvelopes = df['prev_ref_numEnvelopes']
    prev_ref_tEnv0 = df['prev_ref_tEnv0']
    prev_ref_tEnv_last = df['prev_ref_tEnv_last']

    base_feats = pd.DataFrame({
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
        'num_peaks_05': num_peaks_05,
        'prev_ref_frameClass': prev_ref_frameClass,
        'prev_ref_numEnvelopes': prev_ref_numEnvelopes,
        'prev_ref_tEnv0': prev_ref_tEnv0,
        'prev_ref_tEnv_last': prev_ref_tEnv_last,
        'clip': df['clip'],
        'channel': df['channel']
    })

    # 7. Compute 1-frame Future QMF Look-Ahead features per (clip, channel) sequence
    grouped = base_feats.groupby(['clip', 'channel'], sort=False)

    next_max_energy = grouped['max_energy'].shift(-1).fillna(base_feats['max_energy'])
    next_total_energy = grouped['total_energy'].shift(-1).fillna(base_feats['total_energy'])
    next_max_onset_ratio = grouped['max_onset_ratio'].shift(-1).fillna(1.0)
    next_h2_h1_ratio = grouped['h2_h1_ratio'].shift(-1).fillna(1.0)
    next_q1_frac = grouped['q1_frac'].shift(-1).fillna(0.25)
    next_centroid = grouped['centroid'].shift(-1).fillna(16.0)
    next_energy_ratio = next_total_energy / (base_feats['total_energy'] + eps)

    feats = base_feats.drop(columns=['clip', 'channel']).copy()

    feats['next_max_energy'] = next_max_energy
    feats['next_total_energy'] = next_total_energy
    feats['next_max_onset_ratio'] = next_max_onset_ratio
    feats['next_h2_h1_ratio'] = next_h2_h1_ratio
    feats['next_q1_frac'] = next_q1_frac
    feats['next_centroid'] = next_centroid
    feats['next_energy_ratio'] = next_energy_ratio

    return feats, slots

def derive_ref_spec_grid(curr_class, o_slot, prev_tlast):
    """
    Constructs grid parameters matching canonical SBR spec:
    T=16 slots.
    """
    if curr_class == 0: # FIXFIX
        num_env = 2
        t_env = [0, 8, 16]
        f_res = [1, 1]
    elif curr_class == 1: # FIXVAR
        num_env = 2
        b1 = int(o_slot)
        t_env = [0, b1, 16]
        f_res = [1, 1]
    elif curr_class == 2: # VARFIX
        num_env = 2
        t0 = max(0, min(16, int(prev_tlast - 16))) if prev_tlast > 16 else 0
        t_env = [t0, 7, 16]
        f_res = [1, 1]
    else: # VARVAR
        num_env = 4
        t0 = max(0, min(16, int(prev_tlast - 16))) if prev_tlast > 16 else 0
        b1 = int(o_slot)
        b2 = min(12, b1 + 4)
        b3 = min(15, b2 + 4)
        t_env = [t0, b1, b2, b3, 18]
        f_res = [1, 0, 0, 1]

    return num_env, t_env, f_res

def run_canonical_spec_state_machine(df, test_mask, pred_attacks):
    """
    Executes canonical reference SBR state transition table.
    """
    test_df = df[test_mask].copy().reset_index(drop=True)
    test_df['pred_attack'] = pred_attacks

    pred_classes = []
    pred_nenvs = []
    pred_tenvs = []

    groups = test_df.groupby(['clip', 'channel'], sort=False)

    for (clip, ch), group in groups:
        indices = group.index.tolist()

        prev_class = 0 # FIXFIX
        spread = False
        prev_tlast = 16

        for idx in indices:
            attack = test_df.at[idx, 'pred_attack']
            o_slot = test_df.at[idx, 'faac_transientSlot']

            # Canonical Reference Transition Table
            if prev_class == 0: # FIXFIX
                curr_class = 1 if attack else 0
            elif prev_class == 1: # FIXVAR
                if attack:
                    curr_class = 3
                    spread = False
                else:
                    curr_class = 3 if spread else 2
                    spread = False
            elif prev_class == 2: # VARFIX
                curr_class = 1 if attack else 0
            elif prev_class == 3: # VARVAR
                if attack:
                    curr_class = 3
                    spread = False
                else:
                    curr_class = 3 if spread else 2
                    spread = False

            p_ne, p_tenv, p_fres = derive_ref_spec_grid(curr_class, o_slot, prev_tlast)

            pred_classes.append(curr_class)
            pred_nenvs.append(p_ne)
            pred_tenvs.append(p_tenv)

            prev_class = curr_class
            prev_tlast = p_tenv[-1]

    test_df['pred_frameClass'] = pred_classes
    test_df['pred_numEnvelopes'] = pred_nenvs

    return test_df, pred_tenvs

def evaluate_canonical_spec(df_path):
    df = pd.read_csv(df_path)
    feats_df, _ = compute_expanded_features_v3(df)

    # Ground Truth Attack flag:
    gt_attack = (df['ref_frameClass'] == 1) | ((df['ref_frameClass'] == 3) & (df['prev_ref_frameClass'].isin([1, 3])))

    unique_clips = sorted(df['clip'].unique())
    np.random.seed(42)
    train_clips = set(np.random.choice(unique_clips, size=int(len(unique_clips)*0.8), replace=False))
    test_clips = set(unique_clips) - train_clips

    train_mask = df['clip'].isin(train_clips)
    test_mask = df['clip'].isin(test_clips)

    # Features for Attack classifier (including current & next-frame look-ahead)
    X_cols = ['total_energy', 'log_total_energy', 'max_energy', 'peak_to_mean', 'max_onset_ratio', 'max_onset_slot', 'h2_h1_ratio', 'centroid', 'next_max_energy', 'next_total_energy', 'next_max_onset_ratio', 'next_energy_ratio']

    X_train, y_train_att = feats_df.loc[train_mask, X_cols], gt_attack[train_mask]
    X_test, y_test_att = feats_df.loc[test_mask, X_cols], gt_attack[test_mask]

    dt_attack = DecisionTreeClassifier(max_depth=5, random_state=42)
    dt_attack.fit(X_train, y_train_att)

    pred_att_test = dt_attack.predict(X_test)

    print(f"\n==========================================")
    print(f"Evaluating Canonical Reference Spec on {df_path}")
    print(f"==========================================")
    print(f"Attack Classifier Test Acc: {accuracy_score(y_test_att, pred_att_test)*100:.2f}%")
    print(classification_report(y_test_att, pred_att_test, target_names=['No Attack', 'Attack']))

    # 1. State Machine with PREDICTED Attack flags
    spec_pred_df, spec_pred_tenvs = run_canonical_spec_state_machine(df, test_mask, pred_att_test)
    pred_class_acc = accuracy_score(spec_pred_df['ref_frameClass'], spec_pred_df['pred_frameClass'])

    pred_matches = 0
    for i in range(len(spec_pred_df)):
        ref_c = spec_pred_df.at[i, 'ref_frameClass']
        ref_ne = spec_pred_df.at[i, 'ref_numEnvelopes']
        if spec_pred_df.at[i, 'pred_frameClass'] == ref_c and spec_pred_df.at[i, 'pred_numEnvelopes'] == ref_ne:
            p_tenv = spec_pred_tenvs[i]
            ref_tenv = [spec_pred_df.at[i, f'ref_tEnv{j}'] for j in range(ref_ne + 1)]
            borders_ok = True
            for j in range(min(len(p_tenv), len(ref_tenv))):
                if ref_tenv[j] != -1:
                    if abs(p_tenv[j] - ref_tenv[j]) > 2:
                        borders_ok = False
                        break
            if borders_ok:
                pred_matches += 1
    pred_full_match = pred_matches / len(spec_pred_df)

    # 2. State Machine with PERFECT Ground-Truth Attack flags
    gt_att_test = gt_attack[test_mask].values
    spec_gt_df, spec_gt_tenvs = run_canonical_spec_state_machine(df, test_mask, gt_att_test)
    gt_class_acc = accuracy_score(spec_gt_df['ref_frameClass'], spec_gt_df['pred_frameClass'])

    gt_matches = 0
    for i in range(len(spec_gt_df)):
        ref_c = spec_gt_df.at[i, 'ref_frameClass']
        ref_ne = spec_gt_df.at[i, 'ref_numEnvelopes']
        if spec_gt_df.at[i, 'pred_frameClass'] == ref_c and spec_gt_df.at[i, 'pred_numEnvelopes'] == ref_ne:
            p_tenv = spec_gt_tenvs[i]
            ref_tenv = [spec_gt_df.at[i, f'ref_tEnv{j}'] for j in range(ref_ne + 1)]
            borders_ok = True
            for j in range(min(len(p_tenv), len(ref_tenv))):
                if ref_tenv[j] != -1:
                    if abs(p_tenv[j] - ref_tenv[j]) > 2:
                        borders_ok = False
                        break
            if borders_ok:
                gt_matches += 1
    gt_full_match = gt_matches / len(spec_gt_df)

    print("=== Reference State Machine Accuracy Comparison ===")
    print(f"Spec State Machine (GT Attacks)  | frameClass Acc: {gt_class_acc*100:.2f}% | Strict Full-Match: {gt_full_match*100:.2f}%")
    print(f"Spec State Machine (Pred Attacks)| frameClass Acc: {pred_class_acc*100:.2f}% | Strict Full-Match: {pred_full_match*100:.2f}%")

    print("\n=== Reference State Machine (Pred Attacks) Classification Report ===")
    print(classification_report(spec_pred_df['ref_frameClass'], spec_pred_df['pred_frameClass'], target_names=['FIXFIX (0)', 'FIXVAR (1)', 'VARFIX (2)', 'VARVAR (3)']))

    print("=== Reference State Machine (Pred Attacks) Confusion Matrix ===")
    print(confusion_matrix(spec_pred_df['ref_frameClass'], spec_pred_df['pred_frameClass']))

def main():
    evaluate_canonical_spec('probe/dataset/sbr_grid_dataset_v2_64k.csv.gz')
    evaluate_canonical_spec('probe/dataset/sbr_grid_dataset_v2_96k.csv.gz')

if __name__ == '__main__':
    main()
